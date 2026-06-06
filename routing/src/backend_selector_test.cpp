#include "dbmesh/routing/backend_selector.h"

#include "dbmesh/routing/routing_policy.h"

#include <gtest/gtest.h>

#include <set>

namespace dbmesh::routing {

namespace {

BackendConfig backend(const std::string& id, const std::string& group,
                      BackendRole role, int weight = 100) {
  BackendConfig b;
  b.id     = id;
  b.host   = "10.0.0.1";
  b.port   = 3306;
  b.group  = group;
  b.role   = role;
  b.user   = "dbmesh";
  b.weight = static_cast<uint32_t>(weight);
  return b;
}

SchemaConfig schema(const std::string& group,
                    std::optional<dbmesh::RoutingPolicy> policy = std::nullopt) {
  SchemaConfig s;
  s.name           = "billing";
  s.backend_group  = group;
  s.routing_policy = policy;
  return s;
}

RoutingConfig defaults(dbmesh::RoutingPolicy p = dbmesh::RoutingPolicy::ROUND_ROBIN) {
  RoutingConfig c;
  c.default_policy     = p;
  c.replication_aware  = true;
  c.max_replica_lag_ms = 500;
  return c;
}

} // namespace

// ── Policy unit tests ──────────────────────────────────────────────────────

TEST(RoutingPolicies, RoundRobinDistributesEvenly) {
  BackendRegistry reg;
  reg.load({backend("r1", "g", BackendRole::REPLICA),
            backend("r2", "g", BackendRole::REPLICA)});
  auto candidates = reg.get_group_by_role("g", BackendRole::REPLICA);

  auto policy = PolicyFactory::create(dbmesh::RoutingPolicy::ROUND_ROBIN);
  int r1 = 0, r2 = 0;
  for (int i = 0; i < 100; ++i) {
    auto id = policy->select(candidates, {});
    if (id == "r1") ++r1;
    else if (id == "r2") ++r2;
  }
  EXPECT_EQ(r1, 50);
  EXPECT_EQ(r2, 50);
}

TEST(RoutingPolicies, EmptyCandidatesReturnsEmpty) {
  auto policy = PolicyFactory::create(dbmesh::RoutingPolicy::ROUND_ROBIN);
  EXPECT_TRUE(policy->select({}, {}).empty());
}

TEST(RoutingPolicies, SingleCandidateAlwaysChosen) {
  BackendRegistry reg;
  reg.load({backend("only", "g", BackendRole::REPLICA)});
  auto candidates = reg.get_group_by_role("g", BackendRole::REPLICA);
  for (auto p : {dbmesh::RoutingPolicy::ROUND_ROBIN,
                 dbmesh::RoutingPolicy::RANDOM, dbmesh::RoutingPolicy::WEIGHTED,
                 dbmesh::RoutingPolicy::FAILOVER}) {
    auto policy = PolicyFactory::create(p);
    EXPECT_EQ(policy->select(candidates, {}), "only");
  }
}

TEST(RoutingPolicies, WeightedRespectsWeights) {
  BackendRegistry reg;
  reg.load({backend("heavy", "g", BackendRole::REPLICA, /*weight=*/90),
            backend("light", "g", BackendRole::REPLICA, /*weight=*/10)});
  auto candidates = reg.get_group_by_role("g", BackendRole::REPLICA);

  auto policy = PolicyFactory::create(dbmesh::RoutingPolicy::WEIGHTED);
  int heavy = 0, light = 0;
  for (int i = 0; i < 5000; ++i) {
    auto id = policy->select(candidates, {});
    if (id == "heavy") ++heavy;
    else ++light;
  }
  // ~90/10 split; allow generous tolerance for randomness.
  EXPECT_GT(heavy, light * 4);
}

TEST(RoutingPolicies, LeastConnectionsPicksMinimum) {
  BackendRegistry reg;
  reg.load({backend("a", "g", BackendRole::REPLICA),
            backend("b", "g", BackendRole::REPLICA),
            backend("c", "g", BackendRole::REPLICA)});
  auto candidates = reg.get_group_by_role("g", BackendRole::REPLICA);

  RoutingContext ctx;
  ctx.active_connections = [](const BackendID& id) -> std::size_t {
    if (id == "a") return 5;
    if (id == "b") return 2;  // fewest
    return 8;
  };
  auto policy = PolicyFactory::create(dbmesh::RoutingPolicy::LEAST_CONNECTIONS);
  EXPECT_EQ(policy->select(candidates, ctx), "b");
}

TEST(RoutingPolicies, FailoverSkipsFailed) {
  BackendRegistry reg;
  reg.load({backend("primary", "g", BackendRole::REPLICA),
            backend("standby", "g", BackendRole::REPLICA)});
  reg.update_state("primary", BackendState::FAILED);
  auto candidates = reg.get_group_by_role("g", BackendRole::REPLICA);

  auto policy = PolicyFactory::create(dbmesh::RoutingPolicy::FAILOVER);
  EXPECT_EQ(policy->select(candidates, {}), "standby");
}

// ── BackendSelector tests ──────────────────────────────────────────────────

TEST(BackendSelector, RoutesReplicaRoleToReplica) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY),
            backend("r1", "prod", BackendRole::REPLICA)});
  BackendSelector sel(reg, defaults());

  auto r = sel.select(schema("prod"), BackendRole::REPLICA);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r), "r1");
}

TEST(BackendSelector, RoutesPrimaryRoleToPrimary) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY),
            backend("r1", "prod", BackendRole::REPLICA)});
  BackendSelector sel(reg, defaults());

  auto r = sel.select(schema("prod"), BackendRole::PRIMARY);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r), "p");
}

TEST(BackendSelector, FallsBackToPrimaryWhenNoHealthyReplica) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY),
            backend("r1", "prod", BackendRole::REPLICA)});
  reg.update_state("r1", BackendState::FAILED);
  BackendSelector sel(reg, defaults());

  auto r = sel.select(schema("prod"), BackendRole::REPLICA);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r), "p");  // fell back to primary
}

TEST(BackendSelector, ExcludesReplicaExceedingLag) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY),
            backend("r1", "prod", BackendRole::REPLICA),
            backend("r2", "prod", BackendRole::REPLICA)});
  reg.update_lag("r1", 1000);  // over the 500ms threshold
  reg.update_lag("r2", 10);
  BackendSelector sel(reg, defaults());

  // Only r2 is within lag; round-robin over a single candidate → always r2.
  for (int i = 0; i < 10; ++i) {
    auto r = sel.select(schema("prod"), BackendRole::REPLICA);
    ASSERT_TRUE(is_ok(r));
    EXPECT_EQ(get_value(r), "r2");
  }
}

TEST(BackendSelector, NoBackendsInGroupIsError) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY)});
  BackendSelector sel(reg, defaults());

  auto r = sel.select(schema("nonexistent_group"), BackendRole::PRIMARY);
  ASSERT_TRUE(is_err(r));
  EXPECT_EQ(get_error(r), RoutingError::NO_BACKEND_AVAILABLE);
}

TEST(BackendSelector, AllReplicasFailedAndNoPrimaryIsError) {
  BackendRegistry reg;
  reg.load({backend("r1", "prod", BackendRole::REPLICA)});
  reg.update_state("r1", BackendState::FAILED);
  BackendSelector sel(reg, defaults());

  auto r = sel.select(schema("prod"), BackendRole::REPLICA);
  ASSERT_TRUE(is_err(r));
  EXPECT_EQ(get_error(r), RoutingError::NO_BACKEND_AVAILABLE);
}

TEST(BackendSelector, SchemaPolicyOverridesDefault) {
  BackendRegistry reg;
  reg.load({backend("r1", "prod", BackendRole::REPLICA),
            backend("r2", "prod", BackendRole::REPLICA)});
  // Default round-robin, but schema asks for failover.
  BackendSelector sel(reg, defaults(dbmesh::RoutingPolicy::ROUND_ROBIN));
  reg.update_state("r1", BackendState::FAILED);

  auto r = sel.select(schema("prod", dbmesh::RoutingPolicy::FAILOVER),
                      BackendRole::REPLICA);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r), "r2");  // failover skipped the failed r1
}

TEST(BackendSelector, DegradedReplicaExcludedFromReads) {
  BackendRegistry reg;
  reg.load({backend("p", "prod", BackendRole::PRIMARY),
            backend("r1", "prod", BackendRole::REPLICA)});
  reg.update_state("r1", BackendState::DEGRADED);
  BackendSelector sel(reg, defaults());

  // DEGRADED replica excluded → falls back to primary.
  auto r = sel.select(schema("prod"), BackendRole::REPLICA);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r), "p");
}

} // namespace dbmesh::routing
