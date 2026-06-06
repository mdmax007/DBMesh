#ifndef DBMESH_ROUTING_BACKEND_ENTRY_H_
#define DBMESH_ROUTING_BACKEND_ENTRY_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/types.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace dbmesh::routing {

// Authoritative in-memory state of one backend. Identity fields are immutable
// after construction; role/state/lag are atomics mutated in place (no RCU swap)
// so HealthMonitor and FailoverEngine can update them without copying the map
// (architecture.md §23). Stored as shared_ptr inside the BackendRegistry
// snapshot, so in-place atomic updates are visible to all snapshot holders.
struct BackendEntry {
  // ── Identity (immutable) ────────────────────────────────────────────────
  BackendID   id;
  BackendType type = BackendType::MYSQL;
  std::string host;
  uint16_t    port = 0;
  std::string db_user;
  std::string password_ref;  // key into SecretsStore (M1.18); never plaintext
  std::string group;

  // ── Mutable runtime state (atomics) ─────────────────────────────────────
  std::atomic<BackendRole>  role{BackendRole::PRIMARY};
  std::atomic<BackendState> state{BackendState::HEALTHY};
  std::atomic<uint32_t>     replication_lag_ms{0};
  std::atomic<uint64_t>     last_health_check_ts{0};

  // ── Config (read by PoolManager / selector) ─────────────────────────────
  int        weight = 100;
  uint32_t   max_replication_lag_ms = 500;
  PoolConfig pool;

  // ── Versioning (gossip sync) ────────────────────────────────────────────
  uint64_t version = 0;
  NodeID   updated_by;

  BackendEntry() = default;

  explicit BackendEntry(const BackendConfig& c)
      : id(c.id),
        type(c.type),
        host(c.host),
        port(c.port),
        db_user(c.user),
        password_ref(c.id),  // M1.18 swaps this for a SecretsStore ref
        group(c.group),
        role(c.role),
        weight(static_cast<int>(c.weight)),
        max_replication_lag_ms(c.max_replication_lag_ms),
        pool(c.pool) {}
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_BACKEND_ENTRY_H_
