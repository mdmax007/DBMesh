#include "dbmesh/pool/pool_manager.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <vector>

namespace dbmesh::pool {

namespace asio = boost::asio;

PoolManager::PoolManager(asio::any_io_executor ex,
                          std::shared_ptr<const Config> config)
    : ex_(std::move(ex)),
      config_(std::move(config)),
      logger_(Logger::get("pool.manager")) {}

void PoolManager::create_pool_with_factory(const BackendConfig& backend,
                                            ConnectionPool::Factory factory) {
  std::lock_guard lk(mu_);
  if (pools_.count(backend.id)) return;

  auto pool = std::make_unique<ConnectionPool>(
      ex_, backend.id, backend.pool, std::move(factory));
  pool->start_idle_reaper();
  pools_.emplace(backend.id, std::move(pool));
  logger_->info("created pool for backend '" + backend.id + "' (" +
                backend.host + ":" + std::to_string(backend.port) + ")");
}

ConnectionPool* PoolManager::get_pool(const BackendID& id) {
  std::lock_guard lk(mu_);
  auto it = pools_.find(id);
  return it == pools_.end() ? nullptr : it->second.get();
}

asio::awaitable<void> PoolManager::warm_all() {
  std::vector<ConnectionPool*> pools;
  {
    std::lock_guard lk(mu_);
    for (auto& [id, p] : pools_) pools.push_back(p.get());
  }
  for (auto* p : pools) co_await p->warm();
  co_return;
}

asio::awaitable<void> PoolManager::shutdown() {
  std::vector<ConnectionPool*> pools;
  {
    std::lock_guard lk(mu_);
    for (auto& [id, p] : pools_) pools.push_back(p.get());
  }
  for (auto* p : pools) co_await p->drain();
  {
    std::lock_guard lk(mu_);
    pools_.clear();
  }
  co_return;
}

std::size_t PoolManager::pool_count() const {
  std::lock_guard lk(mu_);
  return pools_.size();
}

} // namespace dbmesh::pool
