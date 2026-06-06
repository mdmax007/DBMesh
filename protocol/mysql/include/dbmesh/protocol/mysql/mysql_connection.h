#ifndef DBMESH_PROTOCOL_MYSQL_MYSQL_CONNECTION_H_
#define DBMESH_PROTOCOL_MYSQL_MYSQL_CONNECTION_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/logger.h"
#include "dbmesh/core/result.h"
#include "dbmesh/pool/pool_manager.h"
#include "dbmesh/protocol/mysql/handshake.h"
#include "dbmesh/protocol/mysql/packet_framer.h"
#include "dbmesh/protocol/mysql/types.h"
#include "dbmesh/routing/routing_engine.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dbmesh::protocol::mysql {

// Per-client connection. Lifecycle:
// enable_shared_from_this lets run() capture itself so co_spawn doesn't need
// a lambda wrapper (Clang 14 has known issues with lambda-coroutine captures).
//   1. send_handshake  → HandshakeV10 → client
//   2. recv_response   → HandshakeResponse41 ← client
//   3. authenticate    → OK | ERR
//   4. command_loop    → COM_* handlers until COM_QUIT or disconnect
//
// COM_QUERY is planned by the RoutingEngine, forwarded to the chosen backend
// via the PoolManager, and the result is streamed back to the client.
class MySqlConnection
    : public std::enable_shared_from_this<MySqlConnection> {
 public:
  MySqlConnection(boost::asio::ip::tcp::socket socket,
                  uint32_t connection_id,
                  std::shared_ptr<const Config> config,
                  const routing::RoutingEngine& engine,
                  pool::PoolManager& pool_manager);

  // Coroutine entry. Stores shared_ptr in the coroutine frame so the
  // connection outlives the co_spawn call. Use this instead of run() directly.
  static boost::asio::awaitable<void>
    start(std::shared_ptr<MySqlConnection> conn);

  boost::asio::awaitable<void> run();

 private:
  boost::asio::awaitable<void> do_run();
  boost::asio::awaitable<void> command_loop();

  // Returns true if the connection should be closed after this command.
  boost::asio::awaitable<bool> handle_command(const MySqlPacket& pkt);

  boost::asio::awaitable<void> handle_com_query(
      const std::vector<uint8_t>& payload);
  boost::asio::awaitable<void> handle_com_init_db(
      const std::vector<uint8_t>& payload);
  boost::asio::awaitable<void> handle_com_field_list(
      const std::vector<uint8_t>& payload);

  // Routes a COM_QUERY to a backend and streams the result back to the client.
  boost::asio::awaitable<void> route_and_forward(
      const std::vector<uint8_t>& query_packet, const std::string& sql);

  // Streams a full backend result (OK/ERR, or column defs + rows + EOFs) to
  // the client. Returns false on backend I/O error.
  boost::asio::awaitable<bool> proxy_result(PacketFramer& backend_framer,
                                            boost::asio::ip::tcp::socket& backend);

  // Helpers for sending wire packets.
  boost::asio::awaitable<void> send(const std::vector<uint8_t>& payload);
  boost::asio::awaitable<void> send_ok(uint64_t affected = 0,
                                        uint64_t insert_id = 0,
                                        uint16_t status = 0x0002);
  boost::asio::awaitable<void> send_err(uint16_t code,
                                         std::string_view sql_state,
                                         std::string_view msg);

  boost::asio::ip::tcp::socket    socket_;
  uint32_t                        connection_id_;
  std::shared_ptr<const Config>   config_;
  const routing::RoutingEngine*   engine_;
  pool::PoolManager*              pool_manager_;
  PacketFramer                    framer_;
  uint8_t                         seq_{0};
  uint32_t                        client_caps_{0};
  std::string                     username_;
  std::string                     current_db_;
  bool                            in_transaction_{false};
  bool                            autocommit_{true};
  std::shared_ptr<Logger>         logger_;
};

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_MYSQL_CONNECTION_H_
