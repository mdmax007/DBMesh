#include "dbmesh/pool/pool_manager.h"

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

namespace {

class MockConnection : public BackendConnection {
 public:
  asio::awaitable<bool> validate() override { co_return true; }
  bool is_open() const override { return open_; }
  void close() override { open_ = false; }
 private:
  bool open_ = true;
};

ConnectionPool::Factory always_ok_factory() {
  return []() -> asio::awaitable<
                  Result<std::unique_ptr<BackendConnection>, PoolError>> {
    co_return Ok(std::unique_ptr<BackendConnection>(
        std::make_unique<MockConnection>()));
  };
}

BackendConfig backend(const std::string& id, const std::string& group) {
  BackendConfig b;
  b.id        = id;
  b.host      = "127.0.0.1";
  b.port      = 3306;
  b.group     = group;
  b.user      = "dbmesh";
  b.pool.min  = 0;
  b.pool.max  = 5;
  b.pool.warm = false;
  return b;
}

} // namespace

class PoolManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    LoggingConfig log_cfg;
    log_cfg.level  = "ERROR";
    log_cfg.output = "stdout";
    Logger::init(log_cfg);
    config_ = std::make_shared<Config>();
  }
  void TearDown() override { Logger::shutdown(); }

  asio::io_context              io_;
  std::shared_ptr<const Config> config_;
};

TEST_F(PoolManagerTest, GetPoolReturnsNullForUnknownBackend) {
  PoolManager mgr(io_.get_executor(), config_);
  EXPECT_EQ(mgr.get_pool("nope"), nullptr);
}

TEST_F(PoolManagerTest, CreatePoolRegistersAndIsRetrievable) {
  PoolManager mgr(io_.get_executor(), config_);
  mgr.create_pool_with_factory(backend("b1", "prod"), always_ok_factory());

  auto* p = mgr.get_pool("b1");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->backend_id(), "b1");
  EXPECT_EQ(mgr.pool_count(), 1u);
}

TEST_F(PoolManagerTest, DuplicateCreateIsIgnored) {
  PoolManager mgr(io_.get_executor(), config_);
  mgr.create_pool_with_factory(backend("b1", "prod"), always_ok_factory());
  mgr.create_pool_with_factory(backend("b1", "prod"), always_ok_factory());
  EXPECT_EQ(mgr.pool_count(), 1u);
}

TEST_F(PoolManagerTest, PoolsAreIsolatedPerBackend) {
  PoolManager mgr(io_.get_executor(), config_);
  mgr.create_pool_with_factory(backend("b1", "prod"), always_ok_factory());
  mgr.create_pool_with_factory(backend("b2", "prod"), always_ok_factory());

  asio::co_spawn(
      io_,
      [&]() -> asio::awaitable<void> {
        auto r1 = co_await mgr.get_pool("b1")->acquire();
        if (!is_ok(r1)) { ADD_FAILURE() << "acquire b1 failed"; co_return; }
        EXPECT_EQ(mgr.get_pool("b1")->active_count(), 1u);
        // b2 untouched.
        EXPECT_EQ(mgr.get_pool("b2")->active_count(), 0u);
        // Drain to stop the per-pool idle reapers so io_.run() can return.
        co_await mgr.shutdown();
        co_return;
      },
      asio::detached);
  io_.run();
}

TEST_F(PoolManagerTest, ShutdownDrainsAllPools) {
  PoolManager mgr(io_.get_executor(), config_);
  mgr.create_pool_with_factory(backend("b1", "prod"), always_ok_factory());
  mgr.create_pool_with_factory(backend("b2", "prod"), always_ok_factory());

  asio::co_spawn(
      io_,
      [&]() -> asio::awaitable<void> {
        co_await mgr.get_pool("b1")->warm();
        co_await mgr.shutdown();
        co_return;
      },
      asio::detached);
  io_.run();

  EXPECT_EQ(mgr.pool_count(), 0u);
}

} // namespace dbmesh::pool
