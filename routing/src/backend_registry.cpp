#include "dbmesh/routing/backend_registry.h"

#include <utility>

namespace dbmesh::routing {

namespace {
using ById = std::unordered_map<std::string, std::shared_ptr<BackendEntry>,
                                SvHash, SvEq>;
}

BackendRegistry::BackendRegistry() {
  current_.store(std::make_shared<const BackendSnapshot>(),
                 std::memory_order_release);
}

std::shared_ptr<const BackendSnapshot> BackendRegistry::build(ById by_id) {
  auto snap = std::make_shared<BackendSnapshot>();
  snap->by_id = std::move(by_id);
  for (auto& [id, entry] : snap->by_id)
    snap->by_group[entry->group].push_back(entry);
  return snap;
}

void BackendRegistry::load(const std::vector<BackendConfig>& backends) {
  ById by_id;
  for (const auto& b : backends)
    by_id.emplace(b.id, std::make_shared<BackendEntry>(b));
  {
    std::lock_guard lk(write_lock_);
    current_.store(build(std::move(by_id)), std::memory_order_release);
    version_.fetch_add(1);
  }
}

std::shared_ptr<BackendEntry> BackendRegistry::get(std::string_view id) const {
  auto snap = current_.load(std::memory_order_acquire);
  auto it = snap->by_id.find(id);
  return it == snap->by_id.end() ? nullptr : it->second;
}

std::vector<std::shared_ptr<BackendEntry>> BackendRegistry::get_group(
    std::string_view group) const {
  auto snap = current_.load(std::memory_order_acquire);
  auto it = snap->by_group.find(group);
  return it == snap->by_group.end()
             ? std::vector<std::shared_ptr<BackendEntry>>{}
             : it->second;
}

std::vector<std::shared_ptr<BackendEntry>> BackendRegistry::get_group_by_role(
    std::string_view group, BackendRole role) const {
  auto snap = current_.load(std::memory_order_acquire);
  std::vector<std::shared_ptr<BackendEntry>> out;
  auto it = snap->by_group.find(group);
  if (it == snap->by_group.end()) return out;
  for (const auto& e : it->second)
    if (e->role.load(std::memory_order_acquire) == role) out.push_back(e);
  return out;
}

void BackendRegistry::add(const BackendConfig& backend) {
  std::lock_guard lk(write_lock_);
  auto cur = current_.load(std::memory_order_acquire);
  ById copy = cur->by_id;  // copies shared_ptrs (entries shared)
  copy[backend.id] = std::make_shared<BackendEntry>(backend);
  current_.store(build(std::move(copy)), std::memory_order_release);
  version_.fetch_add(1);
}

void BackendRegistry::remove(std::string_view id) {
  std::lock_guard lk(write_lock_);
  auto cur = current_.load(std::memory_order_acquire);
  if (cur->by_id.find(id) == cur->by_id.end()) return;
  ById copy = cur->by_id;
  copy.erase(copy.find(id));
  current_.store(build(std::move(copy)), std::memory_order_release);
  version_.fetch_add(1);
}

void BackendRegistry::update_state(std::string_view id, BackendState state) {
  if (auto e = get(id)) {
    e->state.store(state, std::memory_order_release);
    version_.fetch_add(1);
  }
}

void BackendRegistry::update_role(std::string_view id, BackendRole role) {
  if (auto e = get(id)) {
    e->role.store(role, std::memory_order_release);
    version_.fetch_add(1);
  }
}

void BackendRegistry::update_lag(std::string_view id, uint32_t lag_ms) {
  if (auto e = get(id)) {
    e->replication_lag_ms.store(lag_ms, std::memory_order_release);
  }
}

std::size_t BackendRegistry::size() const {
  return current_.load(std::memory_order_acquire)->by_id.size();
}

} // namespace dbmesh::routing
