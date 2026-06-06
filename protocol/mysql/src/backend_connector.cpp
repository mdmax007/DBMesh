#include "dbmesh/protocol/mysql/backend_connector.h"

#include "dbmesh/core/logger.h"
#include "dbmesh/protocol/mysql/constants.h"
#include "dbmesh/protocol/mysql/handshake.h"
#include "dbmesh/protocol/mysql/mysql_backend_connection.h"
#include "dbmesh/protocol/mysql/packet_framer.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <array>
#include <string>

namespace dbmesh::protocol::mysql {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using pool::BackendConnection;
using pool::ConnectionPool;
using pool::PoolError;

namespace {

// Capabilities DBMesh advertises when connecting to a backend as a client.
constexpr uint32_t kClientFlags =
    caps::LONG_PASSWORD | caps::PROTOCOL_41 | caps::SECURE_CONNECTION |
    caps::PLUGIN_AUTH | caps::TRANSACTIONS | caps::CONNECT_WITH_DB;

// Reads the salt out of an AuthSwitchRequest payload (0xFE + plugin\0 + salt).
std::array<uint8_t, 20> parse_switch_salt(const std::vector<uint8_t>& payload) {
  std::array<uint8_t, 20> salt{};
  std::size_t pos = 1;
  while (pos < payload.size() && payload[pos] != 0x00) ++pos;  // skip plugin name
  ++pos;                                                       // skip null
  for (std::size_t i = 0; i < 20 && pos < payload.size(); ++i)
    salt[i] = payload[pos++];
  return salt;
}

} // namespace

asio::awaitable<Result<std::unique_ptr<BackendConnection>, PoolError>>
BackendConnector::connect(asio::any_io_executor ex, BackendConfig backend) {
  auto logger = Logger::get("mysql.connector");
  boost::system::error_code ec;

  // ── 1. Resolve + TCP connect ──────────────────────────────────────────
  tcp::resolver resolver(ex);
  auto endpoints = co_await resolver.async_resolve(
      backend.host, std::to_string(backend.port),
      asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    logger->warn("resolve '" + backend.host + "' failed: " + ec.message());
    co_return Err(PoolError::CONNECT_FAILED);
  }

  tcp::socket socket(ex);
  co_await asio::async_connect(socket, endpoints,
                               asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    logger->warn("connect to '" + backend.id + "' failed: " + ec.message());
    co_return Err(PoolError::CONNECT_FAILED);
  }
  socket.set_option(tcp::no_delay(true), ec);

  PacketFramer framer;

  // ── 2. Read server HandshakeV10 ───────────────────────────────────────
  auto hs_pkt = co_await framer.read_packet(socket);
  if (is_err(hs_pkt)) co_return Err(PoolError::CONNECT_FAILED);

  auto hs = HandshakeV10::decode(get_value(hs_pkt).payload);
  if (is_err(hs)) {
    logger->warn("bad handshake from '" + backend.id + "': " + get_error(hs));
    co_return Err(PoolError::CONNECT_FAILED);
  }
  const auto& server_hs = get_value(hs);
  uint32_t backend_thread_id = server_hs.connection_id;

  // ── 3. Send HandshakeResponse41 (mysql_native_password) ───────────────
  HandshakeResponse41 resp;
  resp.capability_flags = kClientFlags;
  resp.max_packet_size  = MAX_PACKET_SIZE;
  resp.character_set    = charset::UTF8MB4;
  resp.username         = backend.user;
  resp.auth_response    = scramble_native_password(backend.password,
                                                   server_hs.auth_data);
  resp.database         = "";  // no initial DB; routing sets it per query (M1.5)
  resp.auth_plugin      = NATIVE_PASSWORD;
  resp.capability_flags &= ~caps::CONNECT_WITH_DB;

  uint8_t seq = static_cast<uint8_t>(get_value(hs_pkt).sequence + 1);
  auto w = co_await framer.write_packet(socket, resp.encode(), seq);
  if (is_err(w)) co_return Err(PoolError::CONNECT_FAILED);

  // ── 4. Read auth result (OK / ERR / AuthSwitchRequest) ────────────────
  auto auth_pkt = co_await framer.read_packet(socket);
  if (is_err(auth_pkt)) co_return Err(PoolError::CONNECT_FAILED);
  const auto& ap = get_value(auth_pkt);

  if (!ap.payload.empty() && ap.payload[0] == 0xFE) {
    // AuthSwitchRequest — recompute native_password with the new salt.
    auto new_salt = parse_switch_salt(ap.payload);
    auto token    = scramble_native_password(backend.password, new_salt);
    uint8_t s2    = static_cast<uint8_t>(ap.sequence + 1);
    auto w2 = co_await framer.write_packet(socket, token, s2);
    if (is_err(w2)) co_return Err(PoolError::CONNECT_FAILED);

    auto final_pkt = co_await framer.read_packet(socket);
    if (is_err(final_pkt)) co_return Err(PoolError::CONNECT_FAILED);
    const auto& fp = get_value(final_pkt);
    if (fp.payload.empty() || fp.payload[0] != OK_PACKET) {
      logger->warn("auth (switch) rejected by '" + backend.id + "'");
      co_return Err(PoolError::CONNECT_FAILED);
    }
  } else if (ap.payload.empty() || ap.payload[0] != OK_PACKET) {
    logger->warn("auth rejected by '" + backend.id +
                 "' (caching_sha2 full auth not supported — use "
                 "mysql_native_password)");
    co_return Err(PoolError::CONNECT_FAILED);
  }

  logger->debug("connected to backend '" + backend.id + "' (thread_id=" +
                std::to_string(backend_thread_id) + ")");

  co_return Ok(std::unique_ptr<BackendConnection>(
      std::make_unique<MySqlBackendConnection>(std::move(socket),
                                               backend_thread_id)));
}

ConnectionPool::Factory BackendConnector::make_factory(asio::any_io_executor ex,
                                                        BackendConfig backend) {
  // The returned callable is NOT a coroutine — it just forwards to connect()
  // (which is). This keeps us clear of Clang 14's lambda-coroutine issues.
  return [ex, backend]()
             -> asio::awaitable<Result<std::unique_ptr<BackendConnection>, PoolError>> {
    return BackendConnector::connect(ex, backend);
  };
}

} // namespace dbmesh::protocol::mysql
