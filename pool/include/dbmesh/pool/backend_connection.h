#ifndef DBMESH_POOL_BACKEND_CONNECTION_H_
#define DBMESH_POOL_BACKEND_CONNECTION_H_

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <utility>  // std::exchange — Boost 1.74 awaitable.hpp relies on it

namespace dbmesh::pool {

// Abstract backend connection. The pool only knows this interface; concrete
// implementations (MySqlBackendConnection, later PgBackendConnection) own the
// socket and protocol state. This indirection makes the pool unit-testable
// with mock connections that perform no real I/O.
class BackendConnection {
 public:
  virtual ~BackendConnection() = default;

  // Health check (COM_PING / SELECT 1). Returns false if the connection is dead.
  virtual boost::asio::awaitable<bool> validate() = 0;

  // True while the underlying socket is open.
  [[nodiscard]] virtual bool is_open() const = 0;

  // Closes the underlying socket. Idempotent.
  virtual void close() = 0;

  // Backend-assigned MySQL connection id / PG PID (for KILL / cancel). 0 if N/A.
  [[nodiscard]] virtual uint32_t backend_thread_id() const { return 0; }
};

} // namespace dbmesh::pool

#endif // DBMESH_POOL_BACKEND_CONNECTION_H_
