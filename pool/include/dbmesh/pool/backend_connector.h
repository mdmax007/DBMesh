#ifndef DBMESH_POOL_BACKEND_CONNECTOR_H_
#define DBMESH_POOL_BACKEND_CONNECTOR_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/result.h"
#include "dbmesh/pool/backend_connection.h"
#include "dbmesh/pool/connection_pool.h"
#include "dbmesh/pool/types.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <memory>

namespace dbmesh::pool {

// Establishes real MySQL/MariaDB backend connections: TCP connect → read
// HandshakeV10 → send HandshakeResponse41 (mysql_native_password) → handle an
// optional AuthSwitchRequest → read OK.
//
// M1.3 scope: mysql_native_password only. The Docker dev backend user is
// created `IDENTIFIED WITH mysql_native_password`. caching_sha2 full auth and
// backend TLS are deferred (M1.9). Connect timeout is also deferred — Boost
// 1.74 lacks the awaitable `||` operator; the OS TCP timeout applies.
class BackendConnector {
 public:
  // One-shot connect + authenticate. Returns an authenticated connection.
  static boost::asio::awaitable<
      Result<std::unique_ptr<BackendConnection>, PoolError>>
  connect(boost::asio::any_io_executor ex, BackendConfig backend);

  // Returns a ConnectionPool::Factory bound to this backend config.
  static ConnectionPool::Factory make_factory(boost::asio::any_io_executor ex,
                                              BackendConfig backend);
};

} // namespace dbmesh::pool

#endif // DBMESH_POOL_BACKEND_CONNECTOR_H_
