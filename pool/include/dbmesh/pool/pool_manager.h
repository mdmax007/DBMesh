#ifndef DBMESH_POOL_POOL_MANAGER_H_
#define DBMESH_POOL_POOL_MANAGER_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/logger.h"
#include "dbmesh/pool/connection_pool.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace dbmesh::pool {

// Owns one ConnectionPool per backend id. Pools are created at startup (and,
// later, on dynamic backend registration via the API). `get_pool` is the hot
// read path; the map is guarded by a mutex because structural changes
// (create/remove) are rare. Milestone 1.17 revisits this with the RCU
// BackendRegistry if read contention becomes an issue.
class PoolManager {
 public:
  PoolManager(boost::asio::any_io_executor ex,
              std::shared_ptr<const Config> config);

  // Creates a pool for `backend` using the real MySQL BackendConnector and
  // starts its idle reaper. No-op if a pool with that id already exists.
  void create_pool(const BackendConfig& backend);

  // Same, but with an injected connection factory (used by tests).
  void create_pool_with_factory(const BackendConfig& backend,
                                ConnectionPool::Factory factory);

  // O(1) lookup. Returns nullptr if no pool exists for `id`.
  [[nodiscard]] ConnectionPool* get_pool(const BackendID& id);

  // Pre-warms every pool (best-effort, runs warms concurrently).
  boost::asio::awaitable<void> warm_all();

  // Drains and removes every pool.
  boost::asio::awaitable<void> shutdown();

  [[nodiscard]] std::size_t pool_count() const;

 private:
  boost::asio::any_io_executor  ex_;
  std::shared_ptr<const Config> config_;

  mutable std::mutex                                              mu_;
  std::unordered_map<BackendID, std::unique_ptr<ConnectionPool>>  pools_;

  std::shared_ptr<Logger> logger_;
};

} // namespace dbmesh::pool

#endif // DBMESH_POOL_POOL_MANAGER_H_
