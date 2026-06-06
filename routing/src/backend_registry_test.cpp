#include "dbmesh/routing/backend_registry.h"

#include <gtest/gtest.h>

#include <thread>

namespace dbmesh::routing {

namespace {
BackendConfig backend(const std::string& id, const std::string& group,
                      BackendRole role) {
  BackendConfig b;
  b.id    = id;
  b.host  = "10.0.0.1";
  b.port  = 3306;
  b.group = group;
  b.role  = role;
  b.user  = "dbmesh";
  return b;
}
} // namespace

TEST(BackendRegistry, GetByIdHitAndMiss) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY)});
  ASSERT_NE(reg.get("p"), nullptr);
  EXPECT_EQ(reg.get("p")->group, "prod");
  EXPECT_EQ(reg.get("absent"), nullptr);
}

TEST(BackendRegistry, GetGroupReturnsAllMembers) {
  BackendRegistry reg;
  reg.load({
      backend("p", "prod", BackendRole::PRIMARY),
      backend("r1", "prod", BackendRole::REPLICA),
      backend("r2", "prod", BackendRole::REPLICA),
      backend("a", "analytics", BackendRole::PRIMARY),
  });
  EXPECT_EQ(reg.get_group("prod").size(), 3u);
  EXPECT_EQ(reg.get_group("analytics").size(), 1u);
  EXPECT_EQ(reg.get_group("nope").size(), 0u);
}

TEST(BackendRegistry, GetGroupByRoleFilters) {
  BackendRegistry reg;
  reg.load({
      backend("p", "prod", BackendRole::PRIMARY),
      backend("r1", "prod", BackendRole::REPLICA),
      backend("r2", "prod", BackendRole::REPLICA),
  });
  EXPECT_EQ(reg.get_group_by_role("prod", BackendRole::PRIMARY).size(), 1u);
  EXPECT_EQ(reg.get_group_by_role("prod", BackendRole::REPLICA).size(), 2u);
}

TEST(BackendRegistry, AddAndRemove) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY)});
  reg.add(backend("r1", "prod", BackendRole::REPLICA));
  EXPECT_EQ(reg.get_group("prod").size(), 2u);
  EXPECT_NE(reg.get("r1"), nullptr);

  reg.remove("r1");
  EXPECT_EQ(reg.get("r1"), nullptr);
  EXPECT_EQ(reg.get_group("prod").size(), 1u);
}

TEST(BackendRegistry, UpdateStateIsVisible) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY)});
  EXPECT_EQ(reg.get("p")->state.load(), BackendState::HEALTHY);
  reg.update_state("p", BackendState::FAILED);
  EXPECT_EQ(reg.get("p")->state.load(), BackendState::FAILED);
}

TEST(BackendRegistry, UpdateRoleReflectedInRoleQuery) {
  BackendRegistry reg;
  reg.load({
      backend("p", "prod", BackendRole::PRIMARY),
      backend("r1", "prod", BackendRole::REPLICA),
  });
  // Promote the replica.
  reg.update_role("r1", BackendRole::PRIMARY);
  EXPECT_EQ(reg.get_group_by_role("prod", BackendRole::PRIMARY).size(), 2u);
  EXPECT_EQ(reg.get_group_by_role("prod", BackendRole::REPLICA).size(), 0u);
}

TEST(BackendRegistry, UpdateLag) {
  BackendRegistry reg;
  reg.load({backend("r1", "prod", BackendRole::REPLICA)});
  reg.update_lag("r1", 1234);
  EXPECT_EQ(reg.get("r1")->replication_lag_ms.load(), 1234u);
}

TEST(BackendRegistry, VersionIncrements) {
  BackendRegistry reg;
  auto v0 = reg.version();
  reg.load({backend("p", "prod", BackendRole::PRIMARY)});
  EXPECT_GT(reg.version(), v0);
  auto v1 = reg.version();
  reg.update_state("p", BackendState::DEGRADED);
  EXPECT_GT(reg.version(), v1);
}

// Concurrent in-place state updates + structural swaps + readers.
TEST(BackendRegistry, ConcurrentStateUpdatesAndReaders) {
  BackendRegistry reg;
  reg.load({
      backend("p", "prod", BackendRole::PRIMARY),
      backend("r1", "prod", BackendRole::REPLICA),
  });

  std::atomic<bool> stop{false};
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&] {
      while (!stop.load()) {
        auto g = reg.get_group_by_role("prod", BackendRole::REPLICA);
        for (auto& e : g) (void)e->state.load();
      }
    });
  }

  for (int i = 0; i < 2000; ++i) {
    reg.update_state("r1", (i % 2) ? BackendState::DEGRADED
                                   : BackendState::HEALTHY);
    if (i % 100 == 0) reg.add(backend("tmp" + std::to_string(i), "prod",
                                      BackendRole::REPLICA));
  }

  stop.store(true);
  for (auto& t : readers) t.join();
  EXPECT_NE(reg.get("r1"), nullptr);
}

} // namespace dbmesh::routing
