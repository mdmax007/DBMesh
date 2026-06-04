#include "dbmesh/core/config.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace dbmesh {

namespace {

// Write YAML content to a temp file and return the path.
std::string write_temp_yaml(const std::string& content) {
  auto path = std::filesystem::temp_directory_path() /
              ("dbmesh_test_" + std::to_string(::getpid()) + ".yaml");
  std::ofstream f(path);
  f << content;
  return path.string();
}

constexpr const char* kMinimalConfig = R"yaml(
node:
  id: node-01
  mode: standalone

listeners:
  mysql:
    enabled: true
    port: 3306

backends:
  - id: mysql-primary
    type: mysql
    host: 10.0.1.10
    port: 3306
    role: primary
    group: prod
    user: dbmesh
    password: secret

schemas:
  - name: default
    backend_group: prod

routing:
  read_write_split: true

session:
  recovery_mode: BASIC
)yaml";

} // namespace

// ── ConfigLoader::load ────────────────────────────────────────────────────

TEST(ConfigLoader, LoadsMinimalConfig) {
  auto path   = write_temp_yaml(kMinimalConfig);
  auto result = ConfigLoader::load(path);
  std::filesystem::remove(path);

  ASSERT_TRUE(is_ok(result)) << get_error(result);
  const auto& cfg = get_value(result);

  EXPECT_EQ(cfg.node.id,   "node-01");
  EXPECT_EQ(cfg.node.mode, NodeMode::STANDALONE);
  EXPECT_TRUE(cfg.listeners.mysql.enabled);
  EXPECT_EQ(cfg.listeners.mysql.port, 3306u);

  ASSERT_EQ(cfg.backends.size(), 1u);
  EXPECT_EQ(cfg.backends[0].id,    "mysql-primary");
  EXPECT_EQ(cfg.backends[0].host,  "10.0.1.10");
  EXPECT_EQ(cfg.backends[0].role,  BackendRole::PRIMARY);
  EXPECT_EQ(cfg.backends[0].group, "prod");

  ASSERT_EQ(cfg.schemas.size(), 1u);
  EXPECT_EQ(cfg.schemas[0].name,          "default");
  EXPECT_EQ(cfg.schemas[0].backend_group, "prod");

  EXPECT_TRUE(cfg.routing.read_write_split);
  EXPECT_EQ(cfg.session.recovery_mode, RecoveryMode::BASIC);
}

TEST(ConfigLoader, ReturnsErrorForMissingFile) {
  auto result = ConfigLoader::load("/nonexistent/path/dbmesh.yaml");
  EXPECT_TRUE(is_err(result));
  EXPECT_NE(get_error(result).find("not found"), std::string::npos);
}

TEST(ConfigLoader, ReturnsErrorForBadYaml) {
  auto path   = write_temp_yaml("{ bad yaml: [unclosed");
  auto result = ConfigLoader::load(path);
  std::filesystem::remove(path);
  EXPECT_TRUE(is_err(result));
}

TEST(ConfigLoader, AppliesDefaults) {
  auto path   = write_temp_yaml(kMinimalConfig);
  auto result = ConfigLoader::load(path);
  std::filesystem::remove(path);

  ASSERT_TRUE(is_ok(result));
  const auto& cfg = get_value(result);

  EXPECT_EQ(cfg.backends[0].pool.min,  10u);
  EXPECT_EQ(cfg.backends[0].pool.max, 100u);
  EXPECT_EQ(cfg.failover.health_check.interval_ms, 100u);
  EXPECT_EQ(cfg.failover.health_check.failure_threshold, 3u);
}

// ── ConfigLoader::validate ────────────────────────────────────────────────

TEST(ConfigValidator, AcceptsValidConfig) {
  auto path   = write_temp_yaml(kMinimalConfig);
  auto result = ConfigLoader::load(path);
  std::filesystem::remove(path);
  ASSERT_TRUE(is_ok(result));
  EXPECT_FALSE(ConfigLoader::validate(get_value(result)).has_value());
}

TEST(ConfigValidator, RejectsEmptyNodeId) {
  constexpr const char* yaml = R"yaml(
node:
  id: ""
  mode: standalone
listeners:
  mysql:
    enabled: false
)yaml";
  auto path   = write_temp_yaml(yaml);
  auto result = ConfigLoader::load(path);
  std::filesystem::remove(path);
  // Validation fires inside load()
  EXPECT_TRUE(is_err(result));
  EXPECT_NE(get_error(result).find("node.id"), std::string::npos);
}

TEST(ConfigValidator, RejectsInvalidRecoveryMode) {
  constexpr const char* yaml = R"yaml(
node:
  id: node-01
  mode: standalone
session:
  recovery_mode: BOGUS
)yaml";
  auto path   = write_temp_yaml(yaml);
  auto result = ConfigLoader::load(path);
  std::filesystem::remove(path);
  EXPECT_TRUE(is_err(result));
  EXPECT_NE(get_error(result).find("recovery_mode"), std::string::npos);
}

TEST(ConfigValidator, RejectsDuplicateBackendIds) {
  constexpr const char* yaml = R"yaml(
node:
  id: node-01
  mode: standalone
listeners:
  mysql:
    enabled: true
    port: 3306
backends:
  - id: dup
    type: mysql
    host: 10.0.0.1
    port: 3306
    role: primary
    group: prod
    user: dbmesh
  - id: dup
    type: mysql
    host: 10.0.0.2
    port: 3306
    role: replica
    group: prod
    user: dbmesh
schemas:
  - name: default
    backend_group: prod
)yaml";
  auto path   = write_temp_yaml(yaml);
  auto result = ConfigLoader::load(path);
  std::filesystem::remove(path);
  EXPECT_TRUE(is_err(result));
  EXPECT_NE(get_error(result).find("duplicate backend"), std::string::npos);
}

TEST(ConfigValidator, RejectsPortZero) {
  constexpr const char* yaml = R"yaml(
node:
  id: node-01
  mode: standalone
listeners:
  mysql:
    enabled: true
    port: 0
backends:
  - id: b1
    type: mysql
    host: 10.0.0.1
    port: 3306
    role: primary
    group: prod
    user: dbmesh
schemas:
  - name: default
    backend_group: prod
)yaml";
  auto path   = write_temp_yaml(yaml);
  auto result = ConfigLoader::load(path);
  std::filesystem::remove(path);
  EXPECT_TRUE(is_err(result));
  EXPECT_NE(get_error(result).find("port"), std::string::npos);
}

// ── ConfigReloader ────────────────────────────────────────────────────────

TEST(ConfigReloader, CallsCallbackOnValidReload) {
  auto path = write_temp_yaml(kMinimalConfig);

  std::shared_ptr<const Config> reloaded;
  ConfigReloader reloader{path, [&](std::shared_ptr<const Config> c) {
    reloaded = std::move(c);
  }};

  reloader.reload();
  std::filesystem::remove(path);

  ASSERT_NE(reloaded, nullptr);
  EXPECT_EQ(reloaded->node.id, "node-01");
}

TEST(ConfigReloader, DoesNotCallCallbackOnInvalidConfig) {
  auto path = write_temp_yaml("{ bad yaml: [");

  bool called = false;
  ConfigReloader reloader{path, [&](std::shared_ptr<const Config>) {
    called = true;
  }};

  reloader.reload();
  std::filesystem::remove(path);

  EXPECT_FALSE(called);
}

} // namespace dbmesh
