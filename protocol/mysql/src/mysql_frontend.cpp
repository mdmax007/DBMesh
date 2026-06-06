#include "dbmesh/protocol/mysql/mysql_frontend.h"

#include "dbmesh/protocol/mysql/codec.h"
#include "dbmesh/protocol/mysql/mysql_connection.h"
#include "dbmesh/protocol/mysql/packets.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

namespace dbmesh::protocol::mysql {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// Free-function coroutine: rejects a socket when the connection pool is full.
// Avoids lambda-coroutine captures (known Clang 14 issue).
static asio::awaitable<void> reject_connection(tcp::socket sock) {
  auto payload = make_err(err::CON_COUNT_ERROR, "08004", "Too many connections");
  std::array<uint8_t, 4> hdr{};
  auto len = static_cast<uint32_t>(payload.size());
  hdr[0] = static_cast<uint8_t>(len & 0xFF);
  hdr[1] = static_cast<uint8_t>((len >> 8) & 0xFF);
  hdr[2] = static_cast<uint8_t>((len >> 16) & 0xFF);
  hdr[3] = 0;  // sequence 0

  boost::system::error_code ec;
  std::array<asio::const_buffer, 2> bufs{asio::buffer(hdr),
                                          asio::buffer(payload)};
  co_await asio::async_write(sock, bufs,
                              asio::redirect_error(asio::use_awaitable, ec));
  sock.close(ec);
}

// ── MySqlFrontend ─────────────────────────────────────────────────────────

MySqlFrontend::MySqlFrontend(asio::io_context& io_context,
                              std::shared_ptr<const Config> config,
                              const routing::RoutingEngine& engine,
                              pool::PoolManager& pool_manager)
    : io_context_(io_context),
      config_(std::move(config)),
      engine_(&engine),
      pool_manager_(&pool_manager),
      acceptor_(io_context_),
      logger_(Logger::get("mysql.frontend")) {}

void MySqlFrontend::start() {
  const auto& cfg = config_->listeners.mysql;

  asio::ip::address addr;
  boost::system::error_code ec;
  addr = asio::ip::make_address(cfg.bind, ec);
  if (ec) addr = asio::ip::address_v4::any();

  tcp::endpoint endpoint(addr, cfg.port);
  acceptor_.open(endpoint.protocol());
  acceptor_.set_option(tcp::acceptor::reuse_address(true));
  acceptor_.bind(endpoint);
  acceptor_.listen(static_cast<int>(cfg.backlog));

  logger_->info("listening on " + cfg.bind + ":" +
                std::to_string(cfg.port));

  asio::co_spawn(io_context_, accept_loop(), asio::detached);
}

void MySqlFrontend::stop() {
  boost::system::error_code ec;
  acceptor_.cancel(ec);
  acceptor_.close(ec);
}

asio::awaitable<void> MySqlFrontend::accept_loop() {
  const uint32_t max_conns = config_->listeners.mysql.max_connections;

  for (;;) {
    boost::system::error_code ec;
    auto socket = co_await acceptor_.async_accept(
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec == asio::error::operation_aborted) co_return;
    if (ec) { logger_->warn("accept: " + ec.message()); continue; }

    if (active_conns_.load() >= max_conns) {
      asio::co_spawn(io_context_,
                     reject_connection(std::move(socket)),
                     asio::detached);
      continue;
    }

    uint32_t id = next_conn_id_++;
    active_conns_++;

    // Use MySqlConnection::run() directly (not wrapped in a lambda) to
    // work around Clang 14's lambda-coroutine capture limitations.
    // run() holds shared_from_this() internally to keep itself alive.
    auto conn = std::make_shared<MySqlConnection>(
        std::move(socket), id, config_, *engine_, *pool_manager_);

    asio::co_spawn(io_context_, MySqlConnection::start(conn), asio::detached);
    // active_conns_ is decremented when the connection coroutine finishes.
    // For M1.2 we don't track precise completion; M1.3+ adds RAII decrement.
    active_conns_--;  // placeholder: decremented immediately for M1.2
  }
}

} // namespace dbmesh::protocol::mysql
