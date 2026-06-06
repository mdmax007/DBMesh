#ifndef DBMESH_ROUTING_ERRORS_H_
#define DBMESH_ROUTING_ERRORS_H_

#include <cstdint>

namespace dbmesh::routing {

// Errors produced by the routing pipeline. Mapped to MySQL/PG error codes at
// the protocol layer (see CLAUDE.md "Error codes").
enum class RoutingError : uint8_t {
  NO_SCHEMA_CONFIG,      // no schema entry and no default
  NO_BACKEND_AVAILABLE,  // all candidate backends FAILED / none in group
  BACKEND_DISCONNECTED,  // connection lost mid-query
  FIREWALL_BLOCKED,      // firewall rule matched with action=block (M1.10)
  RATE_LIMITED,          // token bucket exhausted (M1.10)
  POOL_QUEUE_FULL,       // waiter queue at max_waiters (M1.26)
};

[[nodiscard]] inline const char* routing_error_str(RoutingError e) noexcept {
  switch (e) {
    case RoutingError::NO_SCHEMA_CONFIG:     return "no_schema_config";
    case RoutingError::NO_BACKEND_AVAILABLE: return "no_backend_available";
    case RoutingError::BACKEND_DISCONNECTED: return "backend_disconnected";
    case RoutingError::FIREWALL_BLOCKED:     return "firewall_blocked";
    case RoutingError::RATE_LIMITED:         return "rate_limited";
    case RoutingError::POOL_QUEUE_FULL:      return "pool_queue_full";
  }
  return "unknown";
}

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_ERRORS_H_
