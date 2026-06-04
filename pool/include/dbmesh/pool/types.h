#ifndef DBMESH_POOL_TYPES_H_
#define DBMESH_POOL_TYPES_H_

#include <cstdint>

namespace dbmesh::pool {

enum class PoolError : uint8_t {
  POOL_EXHAUSTED,       // at max connections and no free slot
  CONNECT_FAILED,       // BackendConnector could not establish a connection
  BACKEND_UNAVAILABLE,  // backend marked down
  QUEUE_FULL,           // waiter queue at capacity (Milestone 1.26)
  TIMED_OUT,            // acquire_timeout_ms elapsed
  POOL_SHUTDOWN,        // pool is draining / shut down
};

enum class ConnectionState : uint8_t {
  IDLE,        // in the free list
  IN_USE,      // checked out to a caller
  VALIDATING,  // running a health check
  CLOSED,      // socket closed, awaiting removal
};

[[nodiscard]] inline const char* pool_error_str(PoolError e) noexcept {
  switch (e) {
    case PoolError::POOL_EXHAUSTED:      return "pool_exhausted";
    case PoolError::CONNECT_FAILED:      return "connect_failed";
    case PoolError::BACKEND_UNAVAILABLE: return "backend_unavailable";
    case PoolError::QUEUE_FULL:          return "queue_full";
    case PoolError::TIMED_OUT:           return "timed_out";
    case PoolError::POOL_SHUTDOWN:       return "pool_shutdown";
  }
  return "unknown";
}

} // namespace dbmesh::pool

#endif // DBMESH_POOL_TYPES_H_
