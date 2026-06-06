#ifndef DBMESH_ROUTING_SESSION_VIEW_H_
#define DBMESH_ROUTING_SESSION_VIEW_H_

#include "dbmesh/core/types.h"

#include <optional>
#include <string>

namespace dbmesh::routing {

// The slice of session state the routing pipeline reads. The full SessionState
// (Milestone 1.6) supplies these; keeping routing decoupled from it lets the
// engine be tested in isolation and avoids a dependency on the session module.
struct SessionView {
  std::string              current_schema;        // from USE / COM_INIT_DB
  bool                     in_transaction = false;
  bool                     autocommit     = true;
  std::optional<BackendID> pinned_backend;        // affinity (M1.6)
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_SESSION_VIEW_H_
