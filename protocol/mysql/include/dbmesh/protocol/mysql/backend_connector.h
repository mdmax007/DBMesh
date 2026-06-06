#ifndef DBMESH_PROTOCOL_MYSQL_BACKEND_CONNECTOR_H_
#define DBMESH_PROTOCOL_MYSQL_BACKEND_CONNECTOR_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/result.h"
#include "dbmesh/pool/backend_connection.h"
#include "dbmesh/pool/connection_pool.h"
#include "dbmesh/pool/types.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <memory>

namespace dbmesh::protocol::mysql {

// Establishes real MySQL/MariaDB backend connections: TCP connect → read
// HandshakeV10 → send HandshakeResponse41 (mysql_native_password) → handle an
// optional AuthSwitchRequest → read OK. Produces a pool::BackendConnection for
// the connection pool to manage.
//
// Scope: mysql_native_password only. caching_sha2 full auth and backend TLS
// are deferred (M1.9). Connect timeout is deferred — Boost 1.74 lacks the
// awaitable `||` operator; the OS TCP timeout applies.
class BackendConnector {
 public:
  // One-shot connect + authenticate. Returns an authenticated connection.
  static boost::asio::awaitable<
      Result<std::unique_ptr<pool::BackendConnection>, pool::PoolError>>
  connect(boost::asio::any_io_executor ex, BackendConfig backend);

  // Returns a pool::ConnectionPool::Factory bound to this backend config.
  static pool::ConnectionPool::Factory make_factory(
      boost::asio::any_io_executor ex, BackendConfig backend);
};

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_BACKEND_CONNECTOR_H_
