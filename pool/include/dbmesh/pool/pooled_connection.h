#ifndef DBMESH_POOL_POOLED_CONNECTION_H_
#define DBMESH_POOL_POOLED_CONNECTION_H_

#include "dbmesh/core/types.h"
#include "dbmesh/pool/backend_connection.h"
#include "dbmesh/pool/types.h"

#include <chrono>
#include <cstdint>
#include <memory>

namespace dbmesh::pool {

// A pooled backend connection plus its pool bookkeeping. Owned by the
// ConnectionPool (in its `all_` vector); the free list and active set hold
// raw pointers into that storage.
struct PooledConnection {
  BackendID                            backend_id;
  std::unique_ptr<BackendConnection>   conn;
  std::chrono::steady_clock::time_point connected_at{};
  std::chrono::steady_clock::time_point last_used{};
  uint64_t                             queries_served = 0;
  ConnectionState                      state = ConnectionState::IDLE;

  [[nodiscard]] BackendConnection* operator->() const { return conn.get(); }
  [[nodiscard]] BackendConnection& backend() const { return *conn; }
};

} // namespace dbmesh::pool

#endif // DBMESH_POOL_POOLED_CONNECTION_H_
