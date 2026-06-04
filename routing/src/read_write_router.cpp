#include "dbmesh/routing/read_write_router.h"

namespace dbmesh::routing {

BackendRole ReadWriteRouter::select_role(QueryType type,
                                          const RwSessionState& session) {
  // Inside an open transaction, every statement must go to the primary so the
  // client sees a consistent view (reads must observe its own writes).
  if (session.in_transaction) return BackendRole::PRIMARY;

  switch (type) {
    case QueryType::SELECT:
      // SELECT, and SHOW/EXPLAIN/DESCRIBE (which classify as SELECT), are reads.
      return BackendRole::REPLICA;

    case QueryType::BEGIN:
      // Pin the session to the primary for the transaction's duration.
      return BackendRole::PRIMARY;

    case QueryType::COMMIT:
    case QueryType::ROLLBACK:
      // Transaction boundary — stays on the primary the txn ran on.
      return BackendRole::PRIMARY;

    case QueryType::SET:
    case QueryType::USE:
      // Session-state statements: route to primary (also tracked/replayed).
      return BackendRole::PRIMARY;

    case QueryType::INSERT:
    case QueryType::UPDATE:
    case QueryType::DELETE:
    case QueryType::DDL:
    case QueryType::CALL:
      return BackendRole::PRIMARY;

    case QueryType::UNKNOWN:
    default:
      // Unknown statements are routed to the primary — the safe default.
      return BackendRole::PRIMARY;
  }
}

} // namespace dbmesh::routing
