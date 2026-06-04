#include <utility>  // std::exchange — must precede Boost 1.74 awaitable instantiation

#include "dbmesh/pool/mysql_backend_connection.h"

#include "dbmesh/protocol/mysql/constants.h"

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace dbmesh::pool {

namespace asio = boost::asio;
namespace mysql = protocol::mysql;

MySqlBackendConnection::MySqlBackendConnection(asio::ip::tcp::socket socket,
                                                uint32_t backend_thread_id)
    : socket_(std::move(socket)), backend_thread_id_(backend_thread_id) {}

asio::awaitable<bool> MySqlBackendConnection::validate() {
  if (!socket_.is_open()) co_return false;

  // COM_PING — server replies with an OK packet.
  std::vector<uint8_t> ping = {mysql::cmd::PING};
  uint8_t seq = 0;
  auto w = co_await framer_.write_packet(socket_, ping, seq);
  if (is_err(w)) co_return false;

  auto r = co_await framer_.read_packet(socket_);
  if (is_err(r)) co_return false;

  const auto& pkt = get_value(r);
  // OK packet starts with 0x00; ERR with 0xFF.
  co_return !pkt.payload.empty() && pkt.payload[0] == mysql::OK_PACKET;
}

bool MySqlBackendConnection::is_open() const {
  return socket_.is_open();
}

void MySqlBackendConnection::close() {
  if (socket_.is_open()) {
    boost::system::error_code ec;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
  }
}

} // namespace dbmesh::pool
