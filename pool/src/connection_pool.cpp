#include "dbmesh/pool/connection_pool.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <chrono>

namespace dbmesh::pool {

namespace asio = boost::asio;
using std::chrono::steady_clock;

ConnectionPool::ConnectionPool(asio::any_io_executor ex,
                                BackendID backend_id,
                                PoolConfig config,
                                Factory factory)
    : strand_(asio::make_strand(ex)),
      backend_id_(std::move(backend_id)),
      config_(config),
      factory_(std::move(factory)),
      idle_timer_(strand_),
      logger_(Logger::get("pool")) {}

// ── track / untrack (strand) ──────────────────────────────────────────────

PooledConnection* ConnectionPool::track(std::unique_ptr<BackendConnection> c) {
  auto pc          = std::make_unique<PooledConnection>();
  pc->backend_id   = backend_id_;
  pc->conn         = std::move(c);
  pc->connected_at = steady_clock::now();
  pc->last_used    = pc->connected_at;
  pc->state        = ConnectionState::IDLE;
  PooledConnection* raw = pc.get();
  all_.push_back(std::move(pc));
  return raw;
}

void ConnectionPool::untrack(PooledConnection* c) {
  auto it = std::find_if(all_.begin(), all_.end(),
                         [c](const auto& up) { return up.get() == c; });
  if (it != all_.end()) all_.erase(it);
}

void ConnectionPool::sync_metrics() {
  free_count_.store(free_list_.size());
  active_count_.store(active_.size());
  waiter_count_.store(waiters_.size());
  total_count_.store(all_.size());
}

// ── acquire ───────────────────────────────────────────────────────────────

asio::awaitable<Result<PooledConnection*, PoolError>> ConnectionPool::acquire() {
  // Run the body on the pool strand. Because acquire_impl is co_spawned on
  // strand_, every co_await inside it resumes on strand_ — so all state access
  // is serialised without a mutex.
  co_return co_await asio::co_spawn(strand_, acquire_impl(), asio::use_awaitable);
}

asio::awaitable<Result<PooledConnection*, PoolError>>
ConnectionPool::acquire_impl() {
  if (shutting_down_) co_return Err(PoolError::POOL_SHUTDOWN);

  // Fast path: reuse a free connection.
  if (!free_list_.empty()) {
    auto* c = free_list_.front();
    free_list_.pop_front();
    c->state     = ConnectionState::IN_USE;
    c->last_used = steady_clock::now();
    active_.insert(c);
    total_acquired_.fetch_add(1);
    sync_metrics();
    co_return Ok(c);
  }

  // Open a new connection if under max.
  if (all_.size() < config_.max) {
    auto r = co_await factory_();  // resumes on strand_
    if (is_err(r)) co_return Err(get_error(r));
    auto* c  = track(std::move(get_value(r)));
    c->state = ConnectionState::IN_USE;
    active_.insert(c);
    total_acquired_.fetch_add(1);
    sync_metrics();
    co_return Ok(c);
  }

  // Pool exhausted — enqueue a waiter and wait for a release or timeout.
  auto waiter = std::make_shared<Waiter>(strand_);
  waiters_.push_back(waiter);
  sync_metrics();

  waiter->timer.expires_after(std::chrono::milliseconds(config_.acquire_timeout_ms));
  boost::system::error_code ec;
  co_await waiter->timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));

  // release() sets result and cancels the timer to wake us. If result is set,
  // we succeeded regardless of the timer's error code (strand serialises both).
  if (waiter->result != nullptr) {
    total_acquired_.fetch_add(1);
    co_return Ok(waiter->result);
  }

  // Timed out: remove ourselves from the waiter queue.
  auto it = std::find(waiters_.begin(), waiters_.end(), waiter);
  if (it != waiters_.end()) waiters_.erase(it);
  sync_metrics();
  co_return Err(PoolError::TIMED_OUT);
}

// ── release ───────────────────────────────────────────────────────────────

void ConnectionPool::release(PooledConnection* c, bool healthy) {
  // Non-coroutine handler dispatched to the strand (Clang-14-safe).
  asio::dispatch(strand_, [this, c, healthy]() {
    auto it = active_.find(c);
    if (it == active_.end()) return;  // unknown / double release
    active_.erase(it);

    if (!healthy || shutting_down_ || !c->conn->is_open()) {
      c->conn->close();
      untrack(c);
      sync_metrics();
      return;
    }

    // Hand directly to a waiter if one is queued (zero-copy transfer).
    if (!waiters_.empty()) {
      auto w = waiters_.front();
      waiters_.pop_front();
      c->state = ConnectionState::IN_USE;
      active_.insert(c);
      w->result = c;
      w->timer.cancel();  // wakes acquire_impl
      sync_metrics();
      return;
    }

    // Otherwise return to the free list.
    c->state     = ConnectionState::IDLE;
    c->last_used = steady_clock::now();
    free_list_.push_back(c);
    sync_metrics();
  });
}

// ── warm ──────────────────────────────────────────────────────────────────

asio::awaitable<void> ConnectionPool::warm() {
  co_await asio::co_spawn(strand_, warm_impl(), asio::use_awaitable);
}

asio::awaitable<void> ConnectionPool::warm_impl() {
  for (uint32_t i = 0; i < config_.min && !shutting_down_; ++i) {
    if (all_.size() >= config_.max) break;
    auto r = co_await factory_();
    if (is_err(r)) {
      logger_->warn("warm: connect to '" + backend_id_ + "' failed (" +
                    pool_error_str(get_error(r)) + ")");
      break;  // backend unreachable — stop warming, idle reaper retries later
    }
    auto* c  = track(std::move(get_value(r)));
    c->state = ConnectionState::IDLE;
    free_list_.push_back(c);
    sync_metrics();
  }
  if (!free_list_.empty())
    logger_->info("warmed " + std::to_string(free_list_.size()) +
                  " connection(s) to '" + backend_id_ + "'");
  co_return;
}

// ── drain ─────────────────────────────────────────────────────────────────

asio::awaitable<void> ConnectionPool::drain() {
  co_await asio::co_spawn(strand_, drain_impl(), asio::use_awaitable);
}

asio::awaitable<void> ConnectionPool::drain_impl() {
  shutting_down_ = true;

  for (auto& w : waiters_) {
    w->result = nullptr;
    w->timer.cancel();  // wakes waiters with TIMED_OUT/SHUTDOWN
  }
  waiters_.clear();

  for (auto* c : free_list_) c->conn->close();
  free_list_.clear();
  for (auto* c : active_) c->conn->close();
  active_.clear();
  all_.clear();

  boost::system::error_code ec;
  idle_timer_.cancel(ec);
  sync_metrics();
  co_return;
}

// ── idle reaper ───────────────────────────────────────────────────────────

void ConnectionPool::start_idle_reaper() {
  asio::co_spawn(strand_, idle_reaper_impl(), asio::detached);
}

asio::awaitable<void> ConnectionPool::idle_reaper_impl() {
  using namespace std::chrono;
  const auto period  = seconds(std::max<uint32_t>(1, config_.idle_timeout_sec / 2));
  const auto max_idle = seconds(config_.idle_timeout_sec);

  while (!shutting_down_) {
    idle_timer_.expires_after(period);
    boost::system::error_code ec;
    co_await idle_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    if (ec || shutting_down_) break;  // cancelled by drain()

    auto now = steady_clock::now();
    std::deque<PooledConnection*> keep;
    for (auto* c : free_list_) {
      bool too_old = (now - c->last_used) > max_idle;
      if (too_old && all_.size() > config_.min) {
        c->conn->close();
        untrack(c);
      } else {
        keep.push_back(c);
      }
    }
    free_list_ = std::move(keep);
    sync_metrics();
  }
  co_return;
}

} // namespace dbmesh::pool
