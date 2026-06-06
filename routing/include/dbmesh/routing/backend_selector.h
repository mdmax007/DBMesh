#ifndef DBMESH_ROUTING_BACKEND_SELECTOR_H_
#define DBMESH_ROUTING_BACKEND_SELECTOR_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/result.h"
#include "dbmesh/core/types.h"
#include "dbmesh/routing/backend_registry.h"
#include "dbmesh/routing/errors.h"
#include "dbmesh/routing/routing_policy.h"

#include <array>
#include <memory>
#include <mutex>

namespace dbmesh::routing {

// Picks a concrete backend for a (schema, role) pair: filters the group's
// candidates by role, health, and replication lag, then applies the schema's
// routing policy. Falls back to the primary when no healthy replica is
// available (architecture.md §6).
class BackendSelector {
 public:
  BackendSelector(const BackendRegistry& registry, RoutingConfig defaults);

  [[nodiscard]] Result<BackendID, RoutingError> select(
      const SchemaConfig& schema, BackendRole role,
      const RoutingContext& ctx = {}) const;

 private:
  [[nodiscard]] const RoutingPolicy& policy_for(
      dbmesh::RoutingPolicy p) const;

  const BackendRegistry& registry_;
  RoutingConfig          defaults_;

  // One reusable policy instance per policy enum (round-robin keeps state).
  static constexpr std::size_t kNumPolicies = 7;
  mutable std::array<std::unique_ptr<RoutingPolicy>, kNumPolicies> policies_;
  mutable std::once_flag                                           policies_init_;
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_BACKEND_SELECTOR_H_
