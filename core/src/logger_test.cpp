#include "dbmesh/core/logger.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace dbmesh {

// ── parse_log_level ───────────────────────────────────────────────────────

TEST(LogLevel, ParsesAllLevels) {
  EXPECT_EQ(parse_log_level("TRACE"), LogLevel::TRACE);
  EXPECT_EQ(parse_log_level("DEBUG"), LogLevel::DEBUG);
  EXPECT_EQ(parse_log_level("INFO"),  LogLevel::INFO);
  EXPECT_EQ(parse_log_level("WARN"),  LogLevel::WARN);
  EXPECT_EQ(parse_log_level("ERROR"), LogLevel::ERROR);
  EXPECT_EQ(parse_log_level("FATAL"), LogLevel::FATAL);
}

TEST(LogLevel, ThrowsOnUnknown) {
  EXPECT_THROW(parse_log_level("VERBOSE"), std::runtime_error);
}

// ── log_level_str ─────────────────────────────────────────────────────────

TEST(LogLevel, StringRepresentation) {
  EXPECT_STREQ(log_level_str(LogLevel::INFO),  "INFO ");
  EXPECT_STREQ(log_level_str(LogLevel::WARN),  "WARN ");
  EXPECT_STREQ(log_level_str(LogLevel::ERROR), "ERROR");
}

// ── Logger lifecycle ──────────────────────────────────────────────────────

class LoggerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    LoggingConfig cfg;
    cfg.level  = "TRACE";
    cfg.format = "text";
    cfg.output = "stdout";
    Logger::init(cfg);
  }

  void TearDown() override {
    Logger::shutdown();
  }
};

TEST_F(LoggerTest, GetReturnsSameInstanceForSameComponent) {
  auto a = Logger::get("pool");
  auto b = Logger::get("pool");
  EXPECT_EQ(a.get(), b.get());
}

TEST_F(LoggerTest, GetReturnsDifferentInstancesForDifferentComponents) {
  auto pool    = Logger::get("pool");
  auto routing = Logger::get("routing");
  EXPECT_NE(pool.get(), routing.get());
}

TEST_F(LoggerTest, ComponentNamePreserved) {
  auto logger = Logger::get("routing");
  EXPECT_EQ(logger->component(), "routing");
}

TEST_F(LoggerTest, LogDoesNotBlock) {
  auto logger = Logger::get("test");
  // Send many messages rapidly; log() must return fast (no synchronous I/O).
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 10000; ++i) logger->info("benchmark message");
  auto elapsed = std::chrono::steady_clock::now() - start;
  // 10k enqueues should complete in under 100ms on any hardware.
  EXPECT_LT(
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
      100);
}

TEST_F(LoggerTest, AllConvenienceMethodsCompile) {
  auto logger = Logger::get("test");
  logger->trace("trace");
  logger->debug("debug");
  logger->info("info");
  logger->warn("warn");
  logger->error("error");
  logger->fatal("fatal");
  // Give the writer thread a moment to drain.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// ── Level filtering ───────────────────────────────────────────────────────

// We can't directly inspect what was written without a custom sink, but we
// can verify that the dispatcher respects the min_level without crashing.
TEST(LoggerFiltering, InitWithHighLevelDoesNotCrash) {
  LoggingConfig cfg;
  cfg.level  = "ERROR";
  cfg.format = "json";
  cfg.output = "stdout";
  Logger::init(cfg);

  auto logger = Logger::get("filter_test");
  logger->trace("should be dropped");
  logger->debug("should be dropped");
  logger->info("should be dropped");
  logger->warn("should be dropped");
  logger->error("should pass");

  Logger::shutdown();
}

} // namespace dbmesh
