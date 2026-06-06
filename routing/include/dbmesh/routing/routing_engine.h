#ifndef DBMESH_ROUTING_ROUTING_ENGINE_H_
#define DBMESH_ROUTING_ROUTING_ENGINE_H_

#include "dbmesh/core/result.h"
#include "dbmesh/core/types.h"
#include "dbmesh/routing/backend_selector.h"
#include "dbmesh/routing/errors.h"
#include "dbmesh/routing/routing_policy.h"
#include "dbmesh/routing/schema_registry.h"
#include "dbmesh/routing/session_view.h"

#include <string>
#include <string_view>

namespace dbmesh::routing {

// The outcome of routing a query: which backend to send it to and why. The
// actual pool acquire + query forwarding (the async tail of the pipeline) is
// done by the caller (the protocol frontend), keeping the routing module free
// of any dependency on the pool — and trivially unit-testable.
struct RoutePlan {
  QueryType   query_type;
  SchemaName  schema;        // resolved schema name
  BackendID   backend_id;    // chosen backend
  BackendRole role;          // PRIMARY or REPLICA
};

// Runs the synchronous part of the routing pipeline (architecture.md §6, steps
// 1–7). Steps 8–9 (firewall, rate limit) are hooked here and currently allow
// all (Milestone 1.10). Step 10 (pool acquire) is the caller's job.
class RoutingEngine {
 public:
  RoutingEngine(const SchemaRegistry& schemas, const BackendSelector& selector,
                RoutingConfig routing);

  [[nodiscard]] Result<RoutePlan, RoutingError> plan(
      std::string_view sql, const SessionView& session,
      const RoutingContext& ctx = {}) const;

 private:
  const SchemaRegistry&  schemas_;
  const BackendSelector& selector_;
  RoutingConfig          routing_;
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_ROUTING_ENGINE_H_
