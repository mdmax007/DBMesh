#ifndef DBMESH_ROUTING_BACKEND_REGISTRY_H_
#define DBMESH_ROUTING_BACKEND_REGISTRY_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/types.h"
#include "dbmesh/routing/backend_entry.h"
#include "dbmesh/routing/schema_registry.h"  // SvHash / SvEq

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dbmesh::routing {

// A consistent, immutable view of all backends: a by-id map and a by-group
// index, swapped together under RCU. Entries are shared_ptr so structural
// copies (add/remove) are cheap and in-place atomic state updates remain
// visible across snapshots.
struct BackendSnapshot {
  std::unordered_map<std::string, std::shared_ptr<BackendEntry>, SvHash, SvEq>
      by_id;
  std::unordered_map<std::string,
                     std::vector<std::shared_ptr<BackendEntry>>, SvHash, SvEq>
      by_group;
};

// Peer to SchemaRegistry. Structural changes (add/remove/replace config) go
// through RCU; per-backend health (state/role/lag) is updated in place via the
// entry's atomics — no map copy (architecture.md §23).
class BackendRegistry {
 public:
  BackendRegistry();

  // Replace all backends from config (startup / full reload).
  void load(const std::vector<BackendConfig>& backends);

  // Lock-free snapshot. Hold it for the duration of a read to keep entries
  // alive across a concurrent structural swap.
  [[nodiscard]] std::shared_ptr<const BackendSnapshot> snapshot() const {
    return current_.load(std::memory_order_acquire);
  }

  // O(1) lookup by id. Returns nullptr if absent. Entry stays alive as long as
  // some snapshot referencing it is held; for safety prefer snapshot() in code
  // that suspends.
  [[nodiscard]] std::shared_ptr<BackendEntry> get(std::string_view id) const;

  // All backends in a group (any role / state).
  [[nodiscard]] std::vector<std::shared_ptr<BackendEntry>> get_group(
      std::string_view group) const;

  // All backends in a group with the given role (any state — the selector
  // applies health/lag filtering).
  [[nodiscard]] std::vector<std::shared_ptr<BackendEntry>> get_group_by_role(
      std::string_view group, BackendRole role) const;

  // ── Structural mutations (RCU swap) ─────────────────────────────────────
  void add(const BackendConfig& backend);
  void remove(std::string_view id);

  // ── In-place runtime updates (no RCU; atomic on the live entry) ─────────
  void update_state(std::string_view id, BackendState state);
  void update_role(std::string_view id, BackendRole role);
  void update_lag(std::string_view id, uint32_t lag_ms);

  [[nodiscard]] std::uint64_t version() const { return version_.load(); }
  [[nodiscard]] std::size_t   size() const;

 private:
  // Rebuilds the by_group index from a by_id map. Caller holds write_lock_.
  static std::shared_ptr<const BackendSnapshot> build(
      std::unordered_map<std::string, std::shared_ptr<BackendEntry>, SvHash,
                         SvEq> by_id);

  std::atomic<std::shared_ptr<const BackendSnapshot>> current_;
  std::mutex                                          write_lock_;
  std::atomic<std::uint64_t>                          version_{0};
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_BACKEND_REGISTRY_H_
