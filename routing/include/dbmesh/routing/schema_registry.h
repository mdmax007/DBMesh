#ifndef DBMESH_ROUTING_SCHEMA_REGISTRY_H_
#define DBMESH_ROUTING_SCHEMA_REGISTRY_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dbmesh::routing {

// Transparent hashing so we can look up by string_view without allocating.
struct SvHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
};
struct SvEq {
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const noexcept {
    return a == b;
  }
};

// Read-Copy-Update registry of schema → SchemaConfig. Read millions of times
// per second (the hot path), written rarely. Readers never lock — they load an
// atomic shared_ptr snapshot. Writers serialise on write_lock_, copy the map,
// mutate the copy, then atomically swap it in (architecture.md §6).
class SchemaRegistry {
 public:
  using SchemaMap =
      std::unordered_map<std::string, SchemaConfig, SvHash, SvEq>;

  SchemaRegistry();

  // Replaces the entire registry from config (startup / full reload).
  void load(const std::vector<SchemaConfig>& schemas);

  // O(1), lock-free. Returns the named schema, else the "default" schema, else
  // nullptr if neither exists. The pointer is valid for as long as the caller
  // holds no risk of a concurrent swap — it points into the current snapshot;
  // copy what you need if you keep it across suspension points.
  [[nodiscard]] const SchemaConfig* lookup(std::string_view name) const;

  // Insert or update a single schema (RCU swap).
  void upsert(SchemaConfig config);

  // Remove a schema (RCU swap). The "default" schema cannot be removed.
  void remove(std::string_view name);

  // Monotonic version, incremented on every mutation (for gossip sync).
  [[nodiscard]] std::uint64_t version() const { return version_.load(); }

  [[nodiscard]] std::size_t size() const;

  // Snapshot of all schemas (for gossip / API listing).
  [[nodiscard]] std::vector<SchemaConfig> snapshot() const;

 private:
  std::atomic<std::shared_ptr<const SchemaMap>> current_;
  std::mutex                                    write_lock_;
  std::atomic<std::uint64_t>                    version_{0};
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_SCHEMA_REGISTRY_H_
