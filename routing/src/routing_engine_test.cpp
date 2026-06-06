#include "dbmesh/routing/routing_engine.h"

#include <gtest/gtest.h>

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

SchemaConfig schema(const std::string& name, const std::string& group,
                    std::optional<bool> rw_split = std::nullopt) {
  SchemaConfig s;
  s.name             = name;
  s.backend_group    = group;
  s.read_write_split = rw_split;
  return s;
}

RoutingConfig defaults() {
  RoutingConfig c;
  c.default_policy     = dbmesh::RoutingPolicy::ROUND_ROBIN;
  c.read_write_split   = true;
  c.replication_aware  = true;
  c.max_replica_lag_ms = 500;
  return c;
}

// Test fixture wiring registries + selector + engine over a prod group with a
// primary and two replicas, plus an analytics group.
class RoutingEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    backends_.load({
        backend("prod-primary", "prod", BackendRole::PRIMARY),
        backend("prod-replica-01", "prod", BackendRole::REPLICA),
        backend("prod-replica-02", "prod", BackendRole::REPLICA),
        backend("analytics-primary", "analytics", BackendRole::PRIMARY),
    });
    schemas_.load({
        schema("default", "prod"),
        schema("billing", "prod"),
        schema("analytics", "analytics", /*rw_split=*/false),
    });
    selector_ = std::make_unique<BackendSelector>(backends_, defaults());
    engine_   = std::make_unique<RoutingEngine>(schemas_, *selector_, defaults());
  }

  SessionView session(const std::string& cur = "") {
    SessionView s;
    s.current_schema = cur;
    return s;
  }

  SchemaRegistry                  schemas_;
  BackendRegistry                 backends_;
  std::unique_ptr<BackendSelector> selector_;
  std::unique_ptr<RoutingEngine>   engine_;
};

} // namespace

TEST_F(RoutingEngineTest, RoutesSelectToReplica) {
  auto r = engine_->plan("SELECT * FROM billing.users", session());
  ASSERT_TRUE(is_ok(r));
  const auto& plan = get_value(r);
  EXPECT_EQ(plan.query_type, QueryType::SELECT);
  EXPECT_EQ(plan.schema, "billing");
  EXPECT_EQ(plan.role, BackendRole::REPLICA);
  EXPECT_TRUE(plan.backend_id == "prod-replica-01" ||
              plan.backend_id == "prod-replica-02");
}

TEST_F(RoutingEngineTest, RoutesInsertToPrimary) {
  auto r = engine_->plan("INSERT INTO billing.orders (id) VALUES (1)", session());
  ASSERT_TRUE(is_ok(r));
  const auto& plan = get_value(r);
  EXPECT_EQ(plan.query_type, QueryType::INSERT);
  EXPECT_EQ(plan.role, BackendRole::PRIMARY);
  EXPECT_EQ(plan.backend_id, "prod-primary");
}

TEST_F(RoutingEngineTest, UsesSessionSchemaWhenSqlUnqualified) {
  auto s = session("billing");
  auto r = engine_->plan("SELECT * FROM users", s);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r).schema, "billing");
  EXPECT_EQ(get_value(r).role, BackendRole::REPLICA);
}

TEST_F(RoutingEngineTest, UnknownSchemaFallsBackToDefault) {
  // "mystery" not registered → default schema (group prod).
  auto r = engine_->plan("SELECT * FROM mystery.things", session());
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r).role, BackendRole::REPLICA);
  EXPECT_TRUE(get_value(r).backend_id == "prod-replica-01" ||
              get_value(r).backend_id == "prod-replica-02");
}

TEST_F(RoutingEngineTest, SchemaWithReadWriteSplitDisabledRoutesReadsToPrimary) {
  // analytics has read_write_split=false → even SELECT goes to primary.
  auto s = session("analytics");
  auto r = engine_->plan("SELECT * FROM events", s);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r).role, BackendRole::PRIMARY);
  EXPECT_EQ(get_value(r).backend_id, "analytics-primary");
}

TEST_F(RoutingEngineTest, SelectInTransactionGoesToPrimary) {
  auto s = session("billing");
  s.in_transaction = true;
  auto r = engine_->plan("SELECT * FROM users", s);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r).role, BackendRole::PRIMARY);
  EXPECT_EQ(get_value(r).backend_id, "prod-primary");
}

TEST_F(RoutingEngineTest, PinnedBackendHonouredForPrimaryWork) {
  auto s = session("billing");
  s.in_transaction = true;                 // forces PRIMARY role
  s.pinned_backend = "prod-primary";       // explicit pin
  auto r = engine_->plan("UPDATE users SET a=1", s);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r).backend_id, "prod-primary");
}

TEST_F(RoutingEngineTest, DdlGoesToPrimary) {
  auto s = session("billing");
  auto r = engine_->plan("CREATE TABLE billing.t (id INT)", s);
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r).query_type, QueryType::DDL);
  EXPECT_EQ(get_value(r).role, BackendRole::PRIMARY);
}

TEST_F(RoutingEngineTest, NoBackendAvailableWhenAllReplicasDownAndNoPrimary) {
  backends_.load({backend("r1", "lonely", BackendRole::REPLICA)});
  backends_.update_state("r1", BackendState::FAILED);
  schemas_.upsert(schema("lonely", "lonely"));

  auto s = session("lonely");
  auto r = engine_->plan("SELECT 1 FROM lonely.t", s);
  ASSERT_TRUE(is_err(r));
  EXPECT_EQ(get_error(r), RoutingError::NO_BACKEND_AVAILABLE);
}

TEST_F(RoutingEngineTest, NoSchemaConfigWhenNoDefaultAndUnknown) {
  // Fresh registries with no "default".
  SchemaRegistry s_reg;
  s_reg.load({schema("known", "prod")});
  BackendSelector sel(backends_, defaults());
  RoutingEngine eng(s_reg, sel, defaults());

  auto r = eng.plan("SELECT * FROM unknown.t", session());
  ASSERT_TRUE(is_err(r));
  EXPECT_EQ(get_error(r), RoutingError::NO_SCHEMA_CONFIG);
}

TEST_F(RoutingEngineTest, FailoverPromotionReflectedInRouting) {
  // Kill the primary, promote replica-01; writes must now go to replica-01.
  backends_.update_state("prod-primary", BackendState::FAILED);
  backends_.update_role("prod-primary", BackendRole::REPLICA);
  backends_.update_role("prod-replica-01", BackendRole::PRIMARY);

  auto r = engine_->plan("INSERT INTO billing.orders (id) VALUES (1)", session());
  ASSERT_TRUE(is_ok(r));
  EXPECT_EQ(get_value(r).backend_id, "prod-replica-01");
}

} // namespace dbmesh::routing
