#include "dbmesh/routing/schema_registry.h"

namespace dbmesh::routing {

SchemaRegistry::SchemaRegistry() {
  current_.store(std::make_shared<const SchemaMap>(),
                 std::memory_order_release);
}

void SchemaRegistry::load(const std::vector<SchemaConfig>& schemas) {
  auto next = std::make_shared<SchemaMap>();
  for (const auto& s : schemas) next->emplace(s.name, s);
  {
    std::lock_guard lk(write_lock_);
    current_.store(std::move(next), std::memory_order_release);
    version_.fetch_add(1);
  }
}

const SchemaConfig* SchemaRegistry::lookup(std::string_view name) const {
  auto snap = current_.load(std::memory_order_acquire);
  if (auto it = snap->find(name); it != snap->end()) return &it->second;
  if (auto it = snap->find(std::string_view{"default"}); it != snap->end())
    return &it->second;
  return nullptr;
}

void SchemaRegistry::upsert(SchemaConfig config) {
  std::lock_guard lk(write_lock_);
  auto next = std::make_shared<SchemaMap>(*current_.load(std::memory_order_acquire));
  (*next)[config.name] = std::move(config);
  current_.store(std::move(next), std::memory_order_release);
  version_.fetch_add(1);
}

void SchemaRegistry::remove(std::string_view name) {
  if (name == "default") return;  // the catch-all schema is permanent
  std::lock_guard lk(write_lock_);
  auto cur = current_.load(std::memory_order_acquire);
  if (cur->find(name) == cur->end()) return;  // nothing to do
  auto next = std::make_shared<SchemaMap>(*cur);
  next->erase(next->find(name));
  current_.store(std::move(next), std::memory_order_release);
  version_.fetch_add(1);
}

std::size_t SchemaRegistry::size() const {
  return current_.load(std::memory_order_acquire)->size();
}

std::vector<SchemaConfig> SchemaRegistry::snapshot() const {
  auto snap = current_.load(std::memory_order_acquire);
  std::vector<SchemaConfig> out;
  out.reserve(snap->size());
  for (const auto& [name, cfg] : *snap) out.push_back(cfg);
  return out;
}

} // namespace dbmesh::routing
