#include "dbmesh/routing/read_write_router.h"

#include <gtest/gtest.h>

namespace dbmesh::routing {

using RW = ReadWriteRouter;

namespace {
RwSessionState fresh() { return RwSessionState{}; }  // not in txn, autocommit on
RwSessionState in_txn() {
  RwSessionState s;
  s.in_transaction = true;
  return s;
}
} // namespace

// ── Reads go to replica ───────────────────────────────────────────────────

TEST(ReadWriteRouter, SelectGoesToReplica) {
  EXPECT_EQ(RW::select_role(QueryType::SELECT, fresh()), BackendRole::REPLICA);
}

// ── Writes go to primary ──────────────────────────────────────────────────

TEST(ReadWriteRouter, WritesGoToPrimary) {
  EXPECT_EQ(RW::select_role(QueryType::INSERT, fresh()), BackendRole::PRIMARY);
  EXPECT_EQ(RW::select_role(QueryType::UPDATE, fresh()), BackendRole::PRIMARY);
  EXPECT_EQ(RW::select_role(QueryType::DELETE, fresh()), BackendRole::PRIMARY);
  EXPECT_EQ(RW::select_role(QueryType::DDL, fresh()), BackendRole::PRIMARY);
  EXPECT_EQ(RW::select_role(QueryType::CALL, fresh()), BackendRole::PRIMARY);
}

// ── Transaction pinning ───────────────────────────────────────────────────

TEST(ReadWriteRouter, BeginPinsToPrimary) {
  EXPECT_EQ(RW::select_role(QueryType::BEGIN, fresh()), BackendRole::PRIMARY);
}

TEST(ReadWriteRouter, SelectInsideTransactionGoesToPrimary) {
  // The key read/write-split invariant: reads in a txn must hit the primary.
  EXPECT_EQ(RW::select_role(QueryType::SELECT, in_txn()), BackendRole::PRIMARY);
}

TEST(ReadWriteRouter, CommitAndRollbackGoToPrimary) {
  EXPECT_EQ(RW::select_role(QueryType::COMMIT, in_txn()), BackendRole::PRIMARY);
  EXPECT_EQ(RW::select_role(QueryType::ROLLBACK, in_txn()),
            BackendRole::PRIMARY);
  // Even outside a tracked txn, commit/rollback target the primary.
  EXPECT_EQ(RW::select_role(QueryType::COMMIT, fresh()), BackendRole::PRIMARY);
}

// ── Session-state statements ──────────────────────────────────────────────

TEST(ReadWriteRouter, SetAndUseGoToPrimary) {
  EXPECT_EQ(RW::select_role(QueryType::SET, fresh()), BackendRole::PRIMARY);
  EXPECT_EQ(RW::select_role(QueryType::USE, fresh()), BackendRole::PRIMARY);
}

// ── Unknown is a safe default ─────────────────────────────────────────────

TEST(ReadWriteRouter, UnknownGoesToPrimary) {
  EXPECT_EQ(RW::select_role(QueryType::UNKNOWN, fresh()), BackendRole::PRIMARY);
}

// ── Transaction overrides everything ──────────────────────────────────────

TEST(ReadWriteRouter, TransactionForcesPrimaryRegardlessOfType) {
  EXPECT_EQ(RW::select_role(QueryType::SELECT, in_txn()), BackendRole::PRIMARY);
  EXPECT_EQ(RW::select_role(QueryType::INSERT, in_txn()), BackendRole::PRIMARY);
}

} // namespace dbmesh::routing
