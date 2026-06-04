#ifndef DBMESH_POOL_MYSQL_BACKEND_CONNECTION_H_
#define DBMESH_POOL_MYSQL_BACKEND_CONNECTION_H_

#include "dbmesh/pool/backend_connection.h"
#include "dbmesh/protocol/mysql/packet_framer.h"

#include <boost/asio/ip/tcp.hpp>

#include <cstdint>

namespace dbmesh::pool {

// A live MySQL/MariaDB backend connection (already authenticated). Owns the
// socket; M1.5 adds query forwarding via socket(). validate() issues COM_PING.
class MySqlBackendConnection : public BackendConnection {
 public:
  MySqlBackendConnection(boost::asio::ip::tcp::socket socket,
                         uint32_t backend_thread_id);

  boost::asio::awaitable<bool> validate() override;
  [[nodiscard]] bool          is_open() const override;
  void                        close() override;
  [[nodiscard]] uint32_t      backend_thread_id() const override {
    return backend_thread_id_;
  }

  // Exposed for the routing/forwarding path (Milestone 1.5).
  [[nodiscard]] boost::asio::ip::tcp::socket& socket() { return socket_; }

 private:
  boost::asio::ip::tcp::socket    socket_;
  uint32_t                        backend_thread_id_;
  protocol::mysql::PacketFramer   framer_;
};

} // namespace dbmesh::pool

#endif // DBMESH_POOL_MYSQL_BACKEND_CONNECTION_H_
