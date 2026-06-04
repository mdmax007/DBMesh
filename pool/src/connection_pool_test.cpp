#include "dbmesh/pool/connection_pool.h"

#include "dbmesh/core/logger.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>

namespace dbmesh::pool {

namespace asio = boost::asio;

// GoogleTest ASSERT_* expand to `return;`, which is illegal inside a coroutine.
// These coroutine-safe variants co_return instead.
#define CO_ASSERT_OK(r)                                              \
  if (!is_ok(r)) {                                                   \
    ADD_FAILURE() << "expected Ok, got " << pool_error_str(get_error(r)); \
    co_return;                                                       \
  }
#define CO_ASSERT_ERR(r)                              \
  if (!is_err(r)) {                                   \
    ADD_FAILURE() << "expected Err, got Ok";          \
    co_return;                                        \
  }

// ── Mock backend connection (no real I/O) ─────────────────────────────────

class MockConnection : public BackendConnection {
 public:
  explicit MockConnection(std::atomic<int>& live) : live_(live) { ++live_; }
  ~MockConnection() override { --live_; }

  asio::awaitable<bool> validate() override { co_return open_; }
  bool is_open() const override { return open_; }
  void close() override { open_ = false; }

 private:
  std::atomic<int>& live_;
  bool open_ = true;
};

// Test harness: owns an io_context and counters; builds a factory that mints
// MockConnections (optionally failing the first N attempts).
class ConnectionPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    LoggingConfig log_cfg;
    log_cfg.level  = "ERROR";
    log_cfg.output = "stdout";
    Logger::init(log_cfg);
  }
  void TearDown() override { Logger::shutdown(); }

  ConnectionPool::Factory make_factory() {
    return [this]() -> asio::awaitable<
                        Result<std::unique_ptr<BackendConnection>, PoolError>> {
      ++connect_calls_;
      if (fail_connects_ > 0) {
        --fail_connects_;
        co_return Err(PoolError::CONNECT_FAILED);
      }
      co_return Ok(std::unique_ptr<BackendConnection>(
          std::make_unique<MockConnection>(live_conns_)));
    };
  }

  PoolConfig cfg(uint32_t mn, uint32_t mx, uint32_t acquire_ms = 200,
                 uint32_t idle_sec = 60) {
    PoolConfig c;
    c.min                = mn;
    c.max                = mx;
    c.acquire_timeout_ms = acquire_ms;
    c.idle_timeout_sec   = idle_sec;
    c.warm               = false;
    return c;
  }

  asio::io_context   io_;
  std::atomic<int>   live_conns_{0};
  std::atomic<int>   connect_calls_{0};
  int                fail_connects_ = 0;
};

// Runs a coroutine to completion on the test io_context.
template <typename F>
void run(asio::io_context& io, F&& body) {
  asio::co_spawn(io, std::forward<F>(body), asio::detached);
  io.run();
  io.restart();
}

// ── Tests ─────────────────────────────────────────────────────────────────

TEST_F(ConnectionPoolTest, AcquireOpensNewConnection) {
  ConnectionPool pool(io_.get_executor(), "b1", cfg(0, 5), make_factory());

  run(io_, [&]() -> asio::awaitable<void> {
    auto r = co_await pool.acquire();
    CO_ASSERT_OK(r);
    EXPECT_EQ(pool.active_count(), 1u);
    EXPECT_EQ(pool.total_count(), 1u);
    EXPECT_EQ(live_conns_.load(), 1);
    co_return;
  });
}

TEST_F(ConnectionPoolTest, ReleaseReturnsToFreeListAndReuses) {
  ConnectionPool pool(io_.get_executor(), "b1", cfg(0, 5), make_factory());

  run(io_, [&]() -> asio::awaitable<void> {
    auto r1 = co_await pool.acquire();
    CO_ASSERT_OK(r1);
    auto* c1 = get_value(r1);
    pool.release(c1, true);
    co_return;
  });

  // release() was dispatched; let it run.
  io_.restart();
  run(io_, [&]() -> asio::awaitable<void> {
    EXPECT_EQ(pool.free_count(), 1u);
    EXPECT_EQ(pool.active_count(), 0u);

    auto r2 = co_await pool.acquire();
    CO_ASSERT_OK(r2);
    // Reused the freed connection — no new connect, still one live conn.
    EXPECT_EQ(pool.total_count(), 1u);
    EXPECT_EQ(live_conns_.load(), 1);
    EXPECT_EQ(connect_calls_.load(), 1);
    co_return;
  });
}

TEST_F(ConnectionPoolTest, EnforcesMaxConnections) {
  ConnectionPool pool(io_.get_executor(), "b1", cfg(0, 2, /*acquire*/ 100),
                      make_factory());

  run(io_, [&]() -> asio::awaitable<void> {
    auto a = co_await pool.acquire();
    auto b = co_await pool.acquire();
    CO_ASSERT_OK(a);
    CO_ASSERT_OK(b);
    EXPECT_EQ(pool.active_count(), 2u);
    EXPECT_EQ(pool.total_count(), 2u);

    // Third acquire must time out (no release frees a slot).
    auto c = co_await pool.acquire();
    CO_ASSERT_ERR(c);
    EXPECT_EQ(get_error(c), PoolError::TIMED_OUT);
    co_return;
  });
}

TEST_F(ConnectionPoolTest, WaiterIsSatisfiedByRelease) {
  ConnectionPool pool(io_.get_executor(), "b1", cfg(0, 1, /*acquire*/ 2000),
                      make_factory());

  run(io_, [&]() -> asio::awaitable<void> {
    auto a = co_await pool.acquire();
    CO_ASSERT_OK(a);
    auto* held = get_value(a);

    // Launch a second acquire that will block as a waiter.
    bool waiter_done = false;
    PooledConnection* waiter_conn = nullptr;
    asio::co_spawn(
        io_,
        [&]() -> asio::awaitable<void> {
          auto b = co_await pool.acquire();
          waiter_done = true;
          if (is_ok(b)) waiter_conn = get_value(b);
          co_return;
        },
        asio::detached);

    // Let the waiter enqueue, then release the held connection to wake it.
    asio::steady_timer t(io_, std::chrono::milliseconds(50));
    co_await t.async_wait(asio::use_awaitable);
    EXPECT_EQ(pool.waiter_count(), 1u);

    pool.release(held, true);

    asio::steady_timer t2(io_, std::chrono::milliseconds(50));
    co_await t2.async_wait(asio::use_awaitable);

    EXPECT_TRUE(waiter_done);
    EXPECT_NE(waiter_conn, nullptr);
    EXPECT_EQ(pool.waiter_count(), 0u);
    // Same physical connection handed off (max=1, only one ever created).
    EXPECT_EQ(connect_calls_.load(), 1);
    co_return;
  });
}

TEST_F(ConnectionPoolTest, UnhealthyReleaseClosesConnection) {
  ConnectionPool pool(io_.get_executor(), "b1", cfg(0, 5), make_factory());

  run(io_, [&]() -> asio::awaitable<void> {
    auto r = co_await pool.acquire();
    CO_ASSERT_OK(r);
    pool.release(get_value(r), /*healthy=*/false);
    co_return;
  });

  io_.restart();
  run(io_, [&]() -> asio::awaitable<void> {
    EXPECT_EQ(pool.total_count(), 0u);
    EXPECT_EQ(pool.free_count(), 0u);
    EXPECT_EQ(live_conns_.load(), 0);  // MockConnection destroyed
    co_return;
  });
}

TEST_F(ConnectionPoolTest, AcquirePropagatesConnectFailure) {
  fail_connects_ = 1;
  ConnectionPool pool(io_.get_executor(), "b1", cfg(0, 5), make_factory());

  run(io_, [&]() -> asio::awaitable<void> {
    auto r = co_await pool.acquire();
    CO_ASSERT_ERR(r);
    EXPECT_EQ(get_error(r), PoolError::CONNECT_FAILED);
    EXPECT_EQ(pool.total_count(), 0u);
    co_return;
  });
}

TEST_F(ConnectionPoolTest, WarmPreOpensMinConnections) {
  ConnectionPool pool(io_.get_executor(), "b1", cfg(3, 10), make_factory());

  run(io_, [&]() -> asio::awaitable<void> {
    co_await pool.warm();
    EXPECT_EQ(pool.free_count(), 3u);
    EXPECT_EQ(pool.total_count(), 3u);
    EXPECT_EQ(live_conns_.load(), 3);
    co_return;
  });
}

TEST_F(ConnectionPoolTest, DrainClosesEverything) {
  ConnectionPool pool(io_.get_executor(), "b1", cfg(2, 10), make_factory());

  run(io_, [&]() -> asio::awaitable<void> {
    co_await pool.warm();
    EXPECT_EQ(pool.total_count(), 2u);
    co_await pool.drain();
    EXPECT_EQ(pool.total_count(), 0u);
    EXPECT_EQ(live_conns_.load(), 0);

    // Acquire after drain fails fast.
    auto r = co_await pool.acquire();
    CO_ASSERT_ERR(r);
    EXPECT_EQ(get_error(r), PoolError::POOL_SHUTDOWN);
    co_return;
  });
}

TEST_F(ConnectionPoolTest, IdleReaperClosesExcessIdleConnections) {
  // idle_timeout_sec=1 → reaper runs every ~0.5s, closes conns idle > 1s,
  // keeping at least min(=1).
  ConnectionPool pool(io_.get_executor(), "b1", cfg(1, 10, 200, /*idle*/ 1),
                      make_factory());
  pool.start_idle_reaper();

  run(io_, [&]() -> asio::awaitable<void> {
    // Open 3, release all → 3 idle.
    auto a = co_await pool.acquire();
    auto b = co_await pool.acquire();
    auto c = co_await pool.acquire();
    pool.release(get_value(a), true);
    pool.release(get_value(b), true);
    pool.release(get_value(c), true);

    asio::steady_timer settle(io_, std::chrono::milliseconds(50));
    co_await settle.async_wait(asio::use_awaitable);
    EXPECT_EQ(pool.free_count(), 3u);

    // Wait past the second reaper tick (period=1s, idle threshold=1s): at the
    // t≈2s tick the connections are >1s old and get reaped down to min.
    asio::steady_timer wait(io_, std::chrono::milliseconds(2500));
    co_await wait.async_wait(asio::use_awaitable);

    // Down to min=1.
    EXPECT_EQ(pool.free_count(), 1u);
    EXPECT_EQ(pool.total_count(), 1u);

    co_await pool.drain();
    co_return;
  });
}

} // namespace dbmesh::pool
