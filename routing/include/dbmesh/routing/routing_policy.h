#ifndef DBMESH_ROUTING_ROUTING_POLICY_H_
#define DBMESH_ROUTING_ROUTING_POLICY_H_

#include "dbmesh/core/types.h"
#include "dbmesh/routing/backend_entry.h"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace dbmesh::routing {

// Context handed to policies that need live signals (least_connections,
// latency). Callbacks are optional; a policy falls back to a simpler rule when
// a signal is unavailable.
struct RoutingContext {
  // Active pool connections for a backend (least_connections).
  std::function<std::size_t(const BackendID&)> active_connections;
  // Recent p50 latency in microseconds for a backend (latency policy).
  std::function<uint64_t(const BackendID&)> latency_us;
};

// Selects one backend from a pre-filtered candidate list (already filtered by
// role / health / lag by the BackendSelector). Implementations must handle an
// empty list by returning an empty BackendID.
class RoutingPolicy {
 public:
  virtual ~RoutingPolicy() = default;

  [[nodiscard]] virtual BackendID select(
      const std::vector<std::shared_ptr<BackendEntry>>& candidates,
      const RoutingContext& ctx) const = 0;

  [[nodiscard]] virtual std::string_view name() const = 0;
};

// Creates policy instances by enum. The returned policy may hold state (e.g.
// round-robin counter), so callers keep one instance and reuse it.
// Note: within this namespace `RoutingPolicy` is the interface above; the enum
// is `dbmesh::RoutingPolicy`.
class PolicyFactory {
 public:
  [[nodiscard]] static std::unique_ptr<RoutingPolicy> create(
      dbmesh::RoutingPolicy policy);
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_ROUTING_POLICY_H_
