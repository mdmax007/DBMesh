#include "dbmesh/routing/backend_selector.h"

#include <vector>

namespace dbmesh::routing {

BackendSelector::BackendSelector(const BackendRegistry& registry,
                                  RoutingConfig defaults)
    : registry_(registry), defaults_(defaults) {}

const RoutingPolicy& BackendSelector::policy_for(dbmesh::RoutingPolicy p) const {
  std::call_once(policies_init_, [this] {
    for (std::size_t i = 0; i < kNumPolicies; ++i)
      policies_[i] = PolicyFactory::create(static_cast<dbmesh::RoutingPolicy>(i));
  });
  return *policies_[static_cast<std::size_t>(p)];
}

namespace {

// Healthy for read purposes: HEALTHY only (DEGRADED is excluded from reads).
// For the primary we also accept DEGRADED (writes still go through).
bool usable_for_role(BackendState st, BackendRole role) {
  if (role == BackendRole::PRIMARY)
    return st == BackendState::HEALTHY || st == BackendState::DEGRADED;
  return st == BackendState::HEALTHY;
}

std::vector<std::shared_ptr<BackendEntry>> filter(
    const std::vector<std::shared_ptr<BackendEntry>>& in, BackendRole role,
    uint32_t max_lag_ms, bool replication_aware) {
  std::vector<std::shared_ptr<BackendEntry>> out;
  out.reserve(in.size());
  for (const auto& e : in) {
    if (!usable_for_role(e->state.load(std::memory_order_acquire), role)) continue;
    if (role == BackendRole::REPLICA && replication_aware &&
        e->replication_lag_ms.load(std::memory_order_acquire) > max_lag_ms)
      continue;
    out.push_back(e);
  }
  return out;
}

} // namespace

Result<BackendID, RoutingError> BackendSelector::select(
    const SchemaConfig& schema, BackendRole role,
    const RoutingContext& ctx) const {
  const std::string& group = schema.backend_group;
  const uint32_t max_lag = defaults_.max_replica_lag_ms;

  // Try the requested role first.
  auto candidates = filter(registry_.get_group_by_role(group, role), role,
                           max_lag, defaults_.replication_aware);

  // No healthy replica → fall back to the primary.
  if (candidates.empty() && role == BackendRole::REPLICA) {
    candidates = filter(registry_.get_group_by_role(group, BackendRole::PRIMARY),
                        BackendRole::PRIMARY, max_lag, defaults_.replication_aware);
  }

  if (candidates.empty()) return Err(RoutingError::NO_BACKEND_AVAILABLE);

  dbmesh::RoutingPolicy p =
      schema.routing_policy.value_or(defaults_.default_policy);
  BackendID chosen = policy_for(p).select(candidates, ctx);
  if (chosen.empty()) return Err(RoutingError::NO_BACKEND_AVAILABLE);
  return Ok(std::move(chosen));
}

} // namespace dbmesh::routing
