#ifndef DBMESH_PROTOCOL_MYSQL_MYSQL_FRONTEND_H_
#define DBMESH_PROTOCOL_MYSQL_MYSQL_FRONTEND_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/logger.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <memory>

namespace dbmesh::protocol::mysql {

// Owns the TCP acceptor on the MySQL port (default 3306).
// Spawns one MySqlConnection coroutine per accepted client.
class MySqlFrontend {
 public:
  MySqlFrontend(boost::asio::io_context& io_context,
                std::shared_ptr<const Config> config);

  void start();  // binds, listens, launches accept_loop
  void stop();   // cancels acceptor (in-flight connections finish naturally)

 private:
  boost::asio::awaitable<void> accept_loop();

  boost::asio::io_context&               io_context_;
  std::shared_ptr<const Config>          config_;
  boost::asio::ip::tcp::acceptor         acceptor_;
  std::atomic<uint32_t>                  next_conn_id_{1};
  std::atomic<uint32_t>                  active_conns_{0};
  std::shared_ptr<Logger>                logger_;
};

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_MYSQL_FRONTEND_H_
