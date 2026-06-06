#include "dbmesh/protocol/mysql/mysql_connection.h"

#include "dbmesh/pool/connection_pool.h"
#include "dbmesh/protocol/mysql/constants.h"
#include "dbmesh/protocol/mysql/handshake.h"
#include "dbmesh/protocol/mysql/mysql_backend_connection.h"
#include "dbmesh/protocol/mysql/packets.h"
#include "dbmesh/routing/session_view.h"

#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace dbmesh::protocol::mysql {

namespace asio = boost::asio;

MySqlConnection::MySqlConnection(asio::ip::tcp::socket socket,
                                  uint32_t connection_id,
                                  std::shared_ptr<const Config> config,
                                  const routing::RoutingEngine& engine,
                                  pool::PoolManager& pool_manager)
    : socket_(std::move(socket)),
      connection_id_(connection_id),
      config_(std::move(config)),
      engine_(&engine),
      pool_manager_(&pool_manager),
      logger_(Logger::get("mysql.conn")) {}

asio::awaitable<void> MySqlConnection::start(
    std::shared_ptr<MySqlConnection> conn) {
  // Storing conn in the coroutine frame keeps the connection alive for the
  // entire duration of the handshake and command loop.
  co_await conn->run();
}

asio::awaitable<void> MySqlConnection::run() {
  auto self = shared_from_this();  // belt-and-suspenders
  try {
    co_await do_run();
  } catch (const boost::system::system_error& e) {
    if (e.code() != boost::asio::error::eof &&
        e.code() != boost::asio::error::connection_reset &&
        e.code() != boost::asio::error::broken_pipe) {
      logger_->warn("connection " + std::to_string(connection_id_) +
                    " error: " + e.what());
    }
  } catch (const std::exception& e) {
    logger_->error("connection " + std::to_string(connection_id_) +
                   " fatal: " + e.what());
  }
}

asio::awaitable<void> MySqlConnection::do_run() {
  // Apply socket options
  socket_.set_option(asio::ip::tcp::no_delay(true));

  // ── 1. Send HandshakeV10 ────────────────────────────────────────────
  HandshakeV10 hs;
  hs.connection_id   = connection_id_;
  hs.server_version  = config_->node.name.empty()
                           ? "8.0.30-dbmesh"
                           : "8.0.30-dbmesh-" + config_->node.name;
  hs.generate_auth_data();
  auto auth_data = hs.auth_data;

  seq_ = 0;
  co_await send(hs.encode());

  // ── 2. Receive HandshakeResponse41 ─────────────────────────────────
  seq_ = 1;
  auto pkt_result = co_await framer_.read_packet(socket_);
  if (is_err(pkt_result)) co_return;

  auto parse_result = HandshakeResponse41::decode(get_value(pkt_result).payload);
  if (is_err(parse_result)) {
    co_await send_err(err::ACCESS_DENIED, "28000", "Protocol error");
    co_return;
  }
  auto& resp = get_value(parse_result);
  client_caps_ = resp.capability_flags & caps::SERVER_FLAGS;
  username_    = resp.username;
  current_db_  = resp.database;

  // ── 3. Authenticate ─────────────────────────────────────────────────
  // M1.2 stub: accept any user. Real auth (bcrypt + users.yaml) in M1.9.
  // The wire protocol for mysql_native_password is fully implemented;
  // we just skip the actual user-lookup/password comparison.
  bool authenticated = true;  // TODO(M1.9): call AuthChain::verify()
  (void)auth_data;             // will be passed to verify() in M1.9

  if (!authenticated) {
    co_await send_err(err::ACCESS_DENIED, "28000",
                      "Access denied for user '" + username_ + "'");
    co_return;
  }

  logger_->debug("connection " + std::to_string(connection_id_) +
                 " authenticated as '" + username_ + "'");

  // ── 4. Send OK ──────────────────────────────────────────────────────
  seq_ = 2;
  co_await send_ok();

  // ── 5. Command loop ─────────────────────────────────────────────────
  co_await command_loop();
}

asio::awaitable<void> MySqlConnection::command_loop() {
  for (;;) {
    seq_ = 0;
    auto result = co_await framer_.read_packet(socket_);
    if (is_err(result)) co_return;

    const auto& pkt = get_value(result);
    if (pkt.payload.empty()) co_return;

    bool quit = co_await handle_command(pkt);
    if (quit) co_return;
  }
}

asio::awaitable<bool> MySqlConnection::handle_command(const MySqlPacket& pkt) {
  seq_ = pkt.sequence + 1;
  const uint8_t command = pkt.payload[0];

  switch (command) {
    case cmd::QUIT:
      co_return true;

    case cmd::PING:
      co_await send_ok();
      co_return false;

    case cmd::INIT_DB:
      co_await handle_com_init_db(pkt.payload);
      co_return false;

    case cmd::QUERY:
      co_await handle_com_query(pkt.payload);
      co_return false;

    case cmd::FIELD_LIST:
      co_await handle_com_field_list(pkt.payload);
      co_return false;

    case cmd::STATISTICS: {
      std::string stats = "DBMesh connection_id=" + std::to_string(connection_id_);
      co_await send(std::vector<uint8_t>(stats.begin(), stats.end()));
      co_return false;
    }

    case cmd::SET_OPTION:
      co_await send_ok();
      co_return false;

    case cmd::STMT_PREPARE:
    case cmd::STMT_EXECUTE:
    case cmd::STMT_CLOSE:
    case cmd::STMT_RESET:
      co_await send_err(err::UNKNOWN_COM_ERROR, "08S01",
                        "Binary protocol not yet supported (Milestone 1.19)");
      co_return false;

    default:
      co_await send_err(err::UNKNOWN_COM_ERROR, "08S01",
                        "Unknown command " + std::to_string(command));
      co_return false;
  }
}

// ── COM_INIT_DB ───────────────────────────────────────────────────────────

asio::awaitable<void> MySqlConnection::handle_com_init_db(
    const std::vector<uint8_t>& payload) {
  if (payload.size() > 1)
    current_db_.assign(payload.begin() + 1, payload.end());
  co_await send_ok();
}

// ── COM_FIELD_LIST ────────────────────────────────────────────────────────

asio::awaitable<void> MySqlConnection::handle_com_field_list(
    const std::vector<uint8_t>& /*payload*/) {
  // Return empty field list + EOF
  co_await send(make_eof());
}

// ── COM_QUERY ─────────────────────────────────────────────────────────────

namespace {

std::string normalise_prefix(const std::string& sql) {
  // Uppercased, whitespace-collapsed first ~16 chars — enough to detect the
  // leading keyword for session-local statements.
  std::string out;
  bool in_space = true;
  for (char c : sql) {
    if (out.size() >= 16) break;
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_space && !out.empty()) out += ' ';
      in_space = true;
    } else {
      out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      in_space = false;
    }
  }
  return out;
}

bool starts_with(const std::string& s, const char* p) {
  return s.rfind(p, 0) == 0;
}

bool is_eof_packet(const std::vector<uint8_t>& payload) {
  return !payload.empty() && payload[0] == 0xFE && payload.size() < 9;
}

uint16_t routing_error_to_mysql(routing::RoutingError e) {
  switch (e) {
    case routing::RoutingError::NO_BACKEND_AVAILABLE:
    case routing::RoutingError::POOL_QUEUE_FULL:
      return err::CON_COUNT_ERROR;  // 1040
    case routing::RoutingError::FIREWALL_BLOCKED:
      return err::NOT_ALLOWED_COMMAND;  // 1148
    default:
      return 1105;  // ER_UNKNOWN_ERROR
  }
}

} // namespace

asio::awaitable<void> MySqlConnection::handle_com_query(
    const std::vector<uint8_t>& payload) {
  std::string sql(payload.begin() + 1, payload.end());
  std::string prefix = normalise_prefix(sql);

  logger_->debug("conn " + std::to_string(connection_id_) +
                 " query: " + sql.substr(0, 60));

  // Session-local statements: tracked here and answered locally (true backend
  // replay of session state lands with the trackers in Milestone 1.6).
  if (starts_with(prefix, "SET ")) {
    if (prefix.find("AUTOCOMMIT") != std::string::npos)
      autocommit_ = prefix.find("AUTOCOMMIT=0") == std::string::npos &&
                    prefix.find("AUTOCOMMIT = 0") == std::string::npos;
    co_await send_ok();
    co_return;
  }
  if (starts_with(prefix, "USE ")) {
    std::string db = sql.substr(sql.find_first_not_of(" \t", 3));
    while (!db.empty() && std::isspace(static_cast<unsigned char>(db.back())))
      db.pop_back();
    current_db_ = db;
    co_await send_ok();
    co_return;
  }
  if (starts_with(prefix, "BEGIN") || starts_with(prefix, "START TRANS")) {
    in_transaction_ = true;  // M1.6 adds real backend pinning for transactions
    co_await send_ok();
    co_return;
  }
  if (starts_with(prefix, "COMMIT") || starts_with(prefix, "ROLLBACK")) {
    in_transaction_ = false;
    co_await send_ok();
    co_return;
  }

  co_await route_and_forward(payload, sql);
}

asio::awaitable<void> MySqlConnection::route_and_forward(
    const std::vector<uint8_t>& query_packet, const std::string& sql) {
  // ── Plan the route (synchronous pipeline) ─────────────────────────────
  routing::SessionView session;
  session.current_schema = current_db_;
  session.in_transaction = in_transaction_;
  session.autocommit     = autocommit_;

  auto planned = engine_->plan(sql, session);
  if (is_err(planned)) {
    auto e = get_error(planned);
    co_await send_err(routing_error_to_mysql(e), "HY000",
                      std::string("DBMesh routing: ") +
                          routing::routing_error_str(e));
    co_return;
  }
  const auto& plan = get_value(planned);

  // ── Acquire a backend connection ──────────────────────────────────────
  auto* pool = pool_manager_->get_pool(plan.backend_id);
  if (pool == nullptr) {
    co_await send_err(1105, "HY000",
                      "DBMesh: no pool for backend '" + plan.backend_id + "'");
    co_return;
  }
  auto acquired = co_await pool->acquire();
  if (is_err(acquired)) {
    co_await send_err(err::CON_COUNT_ERROR, "08004",
                      "DBMesh: backend pool unavailable for '" +
                          plan.backend_id + "'");
    co_return;
  }
  pool::PooledConnection* pooled = get_value(acquired);
  auto& backend = static_cast<MySqlBackendConnection&>(pooled->backend());

  logger_->debug("conn " + std::to_string(connection_id_) + " -> backend '" +
                 plan.backend_id + "' (" +
                 (plan.role == BackendRole::PRIMARY ? "primary" : "replica") +
                 ")");

  // ── Forward the raw COM_QUERY and stream the result back ──────────────
  bool ok = true;
  boost::system::error_code fwd_ec;
  try {
    uint8_t bseq = 0;
    auto w = co_await backend.framer().write_packet(backend.socket(),
                                                    query_packet, bseq);
    if (is_err(w)) {
      ok = false;
    } else {
      ok = co_await proxy_result(backend.framer(), backend.socket());
    }
  } catch (const boost::system::system_error& e) {
    fwd_ec = e.code();
    ok = false;
  }

  pool->release(pooled, /*healthy=*/ok);
  pooled->queries_served++;

  if (!ok) {
    // Backend died mid-query. Query retry (M1.8) will handle SELECTs later.
    co_await send_err(err::CON_COUNT_ERROR, "08S01",
                      "DBMesh: backend '" + plan.backend_id +
                          "' disconnected during query");
  }
}

asio::awaitable<bool> MySqlConnection::proxy_result(
    PacketFramer& backend_framer, asio::ip::tcp::socket& backend) {
  // First response packet decides the shape.
  auto first = co_await backend_framer.read_packet(backend);
  if (is_err(first)) co_return false;
  {
    const auto& pkt = get_value(first);
    seq_ = pkt.sequence;
    co_await send(pkt.payload);
    uint8_t b0 = pkt.payload.empty() ? 0 : pkt.payload[0];
    if (b0 == OK_PACKET || b0 == ERR_PACKET || b0 == 0xFB) {
      // OK / ERR / LOCAL INFILE (unsupported) — single-packet response.
      co_return true;
    }
  }

  // Result set: forward column definitions until the first EOF.
  for (;;) {
    auto r = co_await backend_framer.read_packet(backend);
    if (is_err(r)) co_return false;
    const auto& pkt = get_value(r);
    seq_ = pkt.sequence;
    co_await send(pkt.payload);
    if (is_eof_packet(pkt.payload)) break;
  }
  // Forward rows until the terminating EOF.
  for (;;) {
    auto r = co_await backend_framer.read_packet(backend);
    if (is_err(r)) co_return false;
    const auto& pkt = get_value(r);
    seq_ = pkt.sequence;
    co_await send(pkt.payload);
    if (is_eof_packet(pkt.payload)) break;
  }
  co_return true;
}

// ── Wire helpers ──────────────────────────────────────────────────────────

asio::awaitable<void> MySqlConnection::send(const std::vector<uint8_t>& payload) {
  auto r = co_await framer_.write_packet(socket_, payload, seq_);
  if (is_err(r))
    throw boost::system::system_error(boost::asio::error::broken_pipe);
}

asio::awaitable<void> MySqlConnection::send_ok(uint64_t affected,
                                                uint64_t insert_id,
                                                uint16_t status_flags) {
  co_await send(make_ok(affected, insert_id, status_flags));
}

asio::awaitable<void> MySqlConnection::send_err(uint16_t code,
                                                  std::string_view sql_state,
                                                  std::string_view msg) {
  co_await send(make_err(code, sql_state, msg));
}

} // namespace dbmesh::protocol::mysql
