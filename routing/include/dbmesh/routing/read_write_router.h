#ifndef DBMESH_ROUTING_READ_WRITE_ROUTER_H_
#define DBMESH_ROUTING_READ_WRITE_ROUTER_H_

#include "dbmesh/core/types.h"

namespace dbmesh::routing {

// Minimal session view the router needs. The full SessionState (Milestone 1.6)
// will supply these fields; keeping the router decoupled from it lets 1.4 land
// first and keeps the router trivially testable.
struct RwSessionState {
  bool in_transaction = false;  // inside BEGIN..COMMIT
  bool autocommit     = true;   // false ⇒ implicit transactions (M1.20 detail)
};

// Decides whether a query goes to the PRIMARY or a REPLICA based on its type
// and the session's transaction state. Read/write splitting, pure and lock-free.
//
// Session affinity / backend pinning is layered on top of this in the routing
// pipeline (Milestone 1.5/1.6) — this class only answers the role question.
class ReadWriteRouter {
 public:
  [[nodiscard]] static BackendRole select_role(QueryType type,
                                               const RwSessionState& session);
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_READ_WRITE_ROUTER_H_
