#ifndef DBMESH_PROTOCOL_MYSQL_MYSQL_BACKEND_CONNECTION_H_
#define DBMESH_PROTOCOL_MYSQL_MYSQL_BACKEND_CONNECTION_H_

#include "dbmesh/pool/backend_connection.h"
#include "dbmesh/protocol/mysql/packet_framer.h"

#include <boost/asio/ip/tcp.hpp>

#include <cstdint>

namespace dbmesh::protocol::mysql {

// A live, authenticated MySQL/MariaDB backend connection. Implements the
// protocol-agnostic pool::BackendConnection interface so the connection pool
// can manage it. Owns the socket; the query-forwarding path (Milestone 1.5)
// reaches it through socket()/framer(). validate() issues COM_PING.
class MySqlBackendConnection : public pool::BackendConnection {
 public:
  MySqlBackendConnection(boost::asio::ip::tcp::socket socket,
                         uint32_t backend_thread_id);

  boost::asio::awaitable<bool> validate() override;
  [[nodiscard]] bool          is_open() const override;
  void                        close() override;
  [[nodiscard]] uint32_t      backend_thread_id() const override {
    return backend_thread_id_;
  }

  // Used by the query-forwarding path (Milestone 1.5).
  [[nodiscard]] boost::asio::ip::tcp::socket& socket() { return socket_; }
  [[nodiscard]] PacketFramer&                 framer() { return framer_; }

 private:
  boost::asio::ip::tcp::socket socket_;
  uint32_t                     backend_thread_id_;
  PacketFramer                 framer_;
};

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_MYSQL_BACKEND_CONNECTION_H_
