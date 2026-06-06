#include "dbmesh/routing/schema_registry.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace dbmesh::routing {

namespace {
SchemaConfig schema(const std::string& name, const std::string& group) {
  SchemaConfig s;
  s.name          = name;
  s.backend_group = group;
  return s;
}
} // namespace

TEST(SchemaRegistry, LookupHit) {
  SchemaRegistry reg;
  reg.load({schema("billing", "prod"), schema("analytics", "analytics")});

  const auto* c = reg.lookup("billing");
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->backend_group, "prod");
}

TEST(SchemaRegistry, LookupMissFallsBackToDefault) {
  SchemaRegistry reg;
  reg.load({schema("default", "prod"), schema("billing", "billing_grp")});

  const auto* c = reg.lookup("nonexistent");
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->name, "default");
  EXPECT_EQ(c->backend_group, "prod");
}

TEST(SchemaRegistry, LookupMissWithoutDefaultReturnsNull) {
  SchemaRegistry reg;
  reg.load({schema("billing", "prod")});
  EXPECT_EQ(reg.lookup("nope"), nullptr);
}

TEST(SchemaRegistry, LookupByStringViewDoesNotRequireString) {
  SchemaRegistry reg;
  reg.load({schema("billing", "prod")});
  std::string_view sv = "billing";
  EXPECT_NE(reg.lookup(sv), nullptr);
}

TEST(SchemaRegistry, UpsertInsertsAndUpdates) {
  SchemaRegistry reg;
  reg.load({schema("billing", "prod")});

  reg.upsert(schema("analytics", "analytics"));
  EXPECT_NE(reg.lookup("analytics"), nullptr);
  EXPECT_EQ(reg.size(), 2u);

  reg.upsert(schema("billing", "new_group"));
  EXPECT_EQ(reg.lookup("billing")->backend_group, "new_group");
  EXPECT_EQ(reg.size(), 2u);
}

TEST(SchemaRegistry, RemoveDeletesSchema) {
  SchemaRegistry reg;
  reg.load({schema("billing", "prod"), schema("analytics", "a")});
  reg.remove("analytics");
  EXPECT_EQ(reg.lookup("analytics"), nullptr);
  EXPECT_EQ(reg.size(), 1u);
}

TEST(SchemaRegistry, RemoveDefaultIsNoOp) {
  SchemaRegistry reg;
  reg.load({schema("default", "prod")});
  reg.remove("default");
  EXPECT_NE(reg.lookup("default"), nullptr);
}

TEST(SchemaRegistry, VersionIncrementsOnMutation) {
  SchemaRegistry reg;
  auto v0 = reg.version();
  reg.load({schema("billing", "prod")});
  auto v1 = reg.version();
  EXPECT_GT(v1, v0);
  reg.upsert(schema("x", "y"));
  EXPECT_GT(reg.version(), v1);
}

TEST(SchemaRegistry, SnapshotReturnsAll) {
  SchemaRegistry reg;
  reg.load({schema("a", "g1"), schema("b", "g2"), schema("c", "g3")});
  auto snap = reg.snapshot();
  EXPECT_EQ(snap.size(), 3u);
}

// RCU: concurrent readers during writes must never crash or read torn data.
TEST(SchemaRegistry, ConcurrentReadersAndWriter) {
  SchemaRegistry reg;
  reg.load({schema("default", "prod"), schema("billing", "prod")});

  std::atomic<bool> stop{false};
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&] {
      while (!stop.load()) {
        const auto* c = reg.lookup("billing");
        if (c) { volatile auto g = c->backend_group.size(); (void)g; }
        const auto* d = reg.lookup("missing");  // hits default
        if (d) { volatile auto n = d->name.size(); (void)n; }
      }
    });
  }

  for (int i = 0; i < 2000; ++i)
    reg.upsert(schema("billing", "grp" + std::to_string(i)));

  stop.store(true);
  for (auto& t : readers) t.join();
  EXPECT_NE(reg.lookup("billing"), nullptr);
}

} // namespace dbmesh::routing
