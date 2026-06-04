#ifndef DBMESH_POOL_CONNECTION_POOL_H_
#define DBMESH_POOL_CONNECTION_POOL_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/logger.h"
#include "dbmesh/core/result.h"
#include "dbmesh/core/types.h"
#include "dbmesh/pool/backend_connection.h"
#include "dbmesh/pool/pooled_connection.h"
#include "dbmesh/pool/types.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

namespace dbmesh::pool {

// One connection pool per backend. All mutable state is touched only on
// `strand_` — there is no mutex. Public coroutines (acquire/warm/drain) run
// their bodies on the strand via co_spawn; release() dispatches a plain
// (non-coroutine) handler to the strand.
//
// Connection creation is injected via a Factory so the pool is unit-testable
// without real sockets.
class ConnectionPool {
 public:
  using Factory = std::function<
      boost::asio::awaitable<Result<std::unique_ptr<BackendConnection>, PoolError>>()>;

  ConnectionPool(boost::asio::any_io_executor ex,
                 BackendID backend_id,
                 PoolConfig config,
                 Factory factory);

  ConnectionPool(const ConnectionPool&)            = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  // Checks out a connection. Caller must release() it. On exhaustion, waits up
  // to acquire_timeout_ms for a release, then returns TIMED_OUT.
  boost::asio::awaitable<Result<PooledConnection*, PoolError>> acquire();

  // Returns a connection to the pool. `healthy=false` closes it instead of
  // reusing it. Non-blocking; safe to call from any executor (incl. a dtor).
  void release(PooledConnection* conn, bool healthy = true);

  // Pre-opens up to `min` connections. Best-effort: logs and continues on error.
  boost::asio::awaitable<void> warm();

  // Closes every connection, fails all waiters, stops the idle reaper.
  boost::asio::awaitable<void> drain();

  // Launches the background idle-connection reaper on the strand.
  void start_idle_reaper();

  // ── Metrics (atomic; readable from any thread) ──────────────────────────
  [[nodiscard]] std::size_t  free_count() const   { return free_count_.load(); }
  [[nodiscard]] std::size_t  active_count() const { return active_count_.load(); }
  [[nodiscard]] std::size_t  waiter_count() const { return waiter_count_.load(); }
  [[nodiscard]] std::size_t  total_count() const  { return total_count_.load(); }
  [[nodiscard]] std::uint64_t total_acquired() const { return total_acquired_.load(); }

  [[nodiscard]] const BackendID&  backend_id() const { return backend_id_; }
  [[nodiscard]] const PoolConfig& config() const     { return config_; }

 private:
  using Strand = boost::asio::strand<boost::asio::any_io_executor>;

  struct Waiter {
    boost::asio::steady_timer timer;
    PooledConnection*         result = nullptr;
    explicit Waiter(const Strand& s) : timer(s) {}
  };

  // All of these run on the strand.
  boost::asio::awaitable<Result<PooledConnection*, PoolError>> acquire_impl();
  boost::asio::awaitable<void> warm_impl();
  boost::asio::awaitable<void> drain_impl();
  boost::asio::awaitable<void> idle_reaper_impl();

  PooledConnection* track(std::unique_ptr<BackendConnection> c);  // strand
  void              untrack(PooledConnection* c);                 // strand
  void              sync_metrics();                               // strand

  Strand      strand_;
  BackendID   backend_id_;
  PoolConfig  config_;
  Factory     factory_;

  std::vector<std::unique_ptr<PooledConnection>> all_;   // owns every connection
  std::deque<PooledConnection*>                  free_list_;
  std::unordered_set<PooledConnection*>          active_;
  std::deque<std::shared_ptr<Waiter>>            waiters_;
  bool                                           shutting_down_ = false;
  boost::asio::steady_timer                      idle_timer_;

  std::atomic<std::size_t>   free_count_{0};
  std::atomic<std::size_t>   active_count_{0};
  std::atomic<std::size_t>   waiter_count_{0};
  std::atomic<std::size_t>   total_count_{0};
  std::atomic<std::uint64_t> total_acquired_{0};

  std::shared_ptr<Logger> logger_;
};

} // namespace dbmesh::pool

#endif // DBMESH_POOL_CONNECTION_POOL_H_
