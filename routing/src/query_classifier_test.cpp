#include "dbmesh/routing/query_classifier.h"

#include <gtest/gtest.h>

namespace dbmesh::routing {

using QC = QueryClassifier;

// ── Basic DML ─────────────────────────────────────────────────────────────

TEST(QueryClassifier, ClassifiesSelectAsRead) {
  EXPECT_EQ(QC::classify("SELECT * FROM users"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("select 1"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("  SELECT  name  FROM  t"), QueryType::SELECT);
}

TEST(QueryClassifier, ClassifiesInsert) {
  EXPECT_EQ(QC::classify("INSERT INTO t VALUES (1)"), QueryType::INSERT);
  EXPECT_EQ(QC::classify("insert into t set a=1"), QueryType::INSERT);
  EXPECT_EQ(QC::classify("REPLACE INTO t VALUES (1)"), QueryType::INSERT);
}

TEST(QueryClassifier, ClassifiesUpdate) {
  EXPECT_EQ(QC::classify("UPDATE t SET a=1"), QueryType::UPDATE);
  EXPECT_EQ(QC::classify("update billing.orders set x=1 where id=2"),
            QueryType::UPDATE);
}

TEST(QueryClassifier, ClassifiesDelete) {
  EXPECT_EQ(QC::classify("DELETE FROM t WHERE id=1"), QueryType::DELETE);
  EXPECT_EQ(QC::classify("delete from t"), QueryType::DELETE);
}

TEST(QueryClassifier, ClassifiesDdl) {
  EXPECT_EQ(QC::classify("CREATE TABLE t (id INT)"), QueryType::DDL);
  EXPECT_EQ(QC::classify("DROP TABLE t"), QueryType::DDL);
  EXPECT_EQ(QC::classify("ALTER TABLE t ADD c INT"), QueryType::DDL);
  EXPECT_EQ(QC::classify("TRUNCATE t"), QueryType::DDL);
  EXPECT_EQ(QC::classify("RENAME TABLE a TO b"), QueryType::DDL);
  EXPECT_EQ(QC::classify("CREATE DATABASE x"), QueryType::DDL);
  EXPECT_EQ(QC::classify("GRANT SELECT ON *.* TO u"), QueryType::DDL);
}

TEST(QueryClassifier, ClassifiesCall) {
  EXPECT_EQ(QC::classify("CALL my_proc(1)"), QueryType::CALL);
  EXPECT_EQ(QC::classify("DO SLEEP(0)"), QueryType::CALL);
}

// ── Transaction control ───────────────────────────────────────────────────

TEST(QueryClassifier, ClassifiesTransactionControl) {
  EXPECT_EQ(QC::classify("BEGIN"), QueryType::BEGIN);
  EXPECT_EQ(QC::classify("START TRANSACTION"), QueryType::BEGIN);
  EXPECT_EQ(QC::classify("COMMIT"), QueryType::COMMIT);
  EXPECT_EQ(QC::classify("ROLLBACK"), QueryType::ROLLBACK);
}

TEST(QueryClassifier, ClassifiesSetAndUse) {
  EXPECT_EQ(QC::classify("SET autocommit=0"), QueryType::SET);
  EXPECT_EQ(QC::classify("SET NAMES utf8mb4"), QueryType::SET);
  EXPECT_EQ(QC::classify("USE billing"), QueryType::USE);
}

// ── SHOW / EXPLAIN route as reads ─────────────────────────────────────────

TEST(QueryClassifier, ShowExplainDescribeAreReads) {
  EXPECT_EQ(QC::classify("SHOW TABLES"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("SHOW DATABASES"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("EXPLAIN SELECT * FROM t"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("DESCRIBE t"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("DESC t"), QueryType::SELECT);
}

// ── Comments ──────────────────────────────────────────────────────────────

TEST(QueryClassifier, StripsLeadingLineComment) {
  EXPECT_EQ(QC::classify("-- a comment\nSELECT 1"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("# hash comment\nUPDATE t SET a=1"), QueryType::UPDATE);
}

TEST(QueryClassifier, StripsBlockComment) {
  EXPECT_EQ(QC::classify("/* hint */ SELECT 1"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("/* multi\nline */ DELETE FROM t"), QueryType::DELETE);
  EXPECT_EQ(QC::classify("SELECT /* mid */ * FROM t"), QueryType::SELECT);
}

TEST(QueryClassifier, DoesNotStripInsideStrings) {
  // A '--' inside a string literal must not be treated as a comment.
  EXPECT_EQ(QC::classify("SELECT '-- not a comment'"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("INSERT INTO t VALUES ('/* x */')"), QueryType::INSERT);
}

// ── Leading parenthesis ───────────────────────────────────────────────────

TEST(QueryClassifier, HandlesLeadingParen) {
  EXPECT_EQ(QC::classify("(SELECT 1) UNION (SELECT 2)"), QueryType::SELECT);
}

// ── CTEs ──────────────────────────────────────────────────────────────────

TEST(QueryClassifier, ClassifiesCteSelect) {
  EXPECT_EQ(QC::classify("WITH cte AS (SELECT 1) SELECT * FROM cte"),
            QueryType::SELECT);
}

TEST(QueryClassifier, ClassifiesCteInsert) {
  EXPECT_EQ(
      QC::classify("WITH cte AS (SELECT 1) INSERT INTO t SELECT * FROM cte"),
      QueryType::INSERT);
}

TEST(QueryClassifier, ClassifiesCteDelete) {
  EXPECT_EQ(
      QC::classify("WITH c AS (SELECT id FROM t) DELETE FROM t WHERE id IN "
                   "(SELECT id FROM c)"),
      QueryType::DELETE);
}

TEST(QueryClassifier, ClassifiesNestedCteSelect) {
  EXPECT_EQ(QC::classify("WITH a AS (SELECT 1), b AS (SELECT 2) "
                         "SELECT * FROM a JOIN b"),
            QueryType::SELECT);
}

// ── Multi-statement escalation ────────────────────────────────────────────

TEST(QueryClassifier, MultiStatementEscalatesToWrite) {
  EXPECT_EQ(QC::classify("SELECT 1; INSERT INTO t VALUES (1)"),
            QueryType::INSERT);
  EXPECT_EQ(QC::classify("SET names utf8; SELECT 1; UPDATE t SET a=1"),
            QueryType::UPDATE);
}

TEST(QueryClassifier, MultiStatementAllReadsStaysRead) {
  EXPECT_EQ(QC::classify("SELECT 1; SELECT 2; SHOW TABLES"), QueryType::SELECT);
}

TEST(QueryClassifier, TrailingSemicolonSingleStatement) {
  EXPECT_EQ(QC::classify("SELECT 1;"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("DELETE FROM t;"), QueryType::DELETE);
}

TEST(QueryClassifier, SemicolonInsideStringIsNotASplit) {
  EXPECT_EQ(QC::classify("SELECT 'a;b;c'"), QueryType::SELECT);
}

// ── Case insensitivity & whitespace ───────────────────────────────────────

TEST(QueryClassifier, CaseInsensitive) {
  EXPECT_EQ(QC::classify("sElEcT 1"), QueryType::SELECT);
  EXPECT_EQ(QC::classify("INSERT into t values (1)"), QueryType::INSERT);
}

TEST(QueryClassifier, HandlesTabsAndNewlines) {
  EXPECT_EQ(QC::classify("\n\t  SELECT 1"), QueryType::SELECT);
}

// ── Edge cases ────────────────────────────────────────────────────────────

TEST(QueryClassifier, EmptyAndWhitespaceAreUnknown) {
  EXPECT_EQ(QC::classify(""), QueryType::UNKNOWN);
  EXPECT_EQ(QC::classify("   \n\t "), QueryType::UNKNOWN);
  EXPECT_EQ(QC::classify("-- only a comment"), QueryType::UNKNOWN);
}

TEST(QueryClassifier, UnknownKeywordIsUnknown) {
  EXPECT_EQ(QC::classify("FROBNICATE t"), QueryType::UNKNOWN);
  EXPECT_EQ(QC::classify("12345"), QueryType::UNKNOWN);
}

TEST(QueryClassifier, AnalyzeOptimizeAreDdl) {
  EXPECT_EQ(QC::classify("ANALYZE TABLE t"), QueryType::DDL);
  EXPECT_EQ(QC::classify("OPTIMIZE TABLE t"), QueryType::DDL);
  EXPECT_EQ(QC::classify("LOCK TABLES t WRITE"), QueryType::DDL);
}

TEST(QueryClassifier, KeywordPrefixIsNotMatched) {
  // "selectfoo" is not "select".
  EXPECT_EQ(QC::classify("selecting_is_not_a_keyword"), QueryType::UNKNOWN);
  EXPECT_EQ(QC::classify("updates_table_name foo"), QueryType::UNKNOWN);
}

} // namespace dbmesh::routing
