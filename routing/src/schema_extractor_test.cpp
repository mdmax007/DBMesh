#include "dbmesh/routing/schema_extractor.h"

#include <gtest/gtest.h>

namespace dbmesh::routing {

using SE = SchemaExtractor;

// ── SELECT ... FROM db.table ──────────────────────────────────────────────

TEST(SchemaExtractor, ExtractsFromQualifiedSelect) {
  EXPECT_EQ(SE::extract("SELECT * FROM billing.users"), "billing");
  EXPECT_EQ(SE::extract("select a,b from analytics.events where x=1"),
            "analytics");
}

TEST(SchemaExtractor, UnqualifiedSelectHasNoSchema) {
  EXPECT_FALSE(SE::extract("SELECT * FROM users").has_value());
  EXPECT_FALSE(SE::extract("SELECT 1").has_value());
}

// ── INSERT / UPDATE / DELETE ──────────────────────────────────────────────

TEST(SchemaExtractor, ExtractsInsertInto) {
  EXPECT_EQ(SE::extract("INSERT INTO billing.orders (id) VALUES (1)"),
            "billing");
}

TEST(SchemaExtractor, ExtractsUpdate) {
  EXPECT_EQ(SE::extract("UPDATE billing.orders SET total=1 WHERE id=2"),
            "billing");
}

TEST(SchemaExtractor, ExtractsDeleteFrom) {
  EXPECT_EQ(SE::extract("DELETE FROM analytics.events WHERE ts<now()"),
            "analytics");
}

// ── JOINs ─────────────────────────────────────────────────────────────────

TEST(SchemaExtractor, ExtractsFirstSchemaInJoin) {
  EXPECT_EQ(SE::extract("SELECT * FROM billing.a JOIN billing.b ON a.id=b.id"),
            "billing");
}

TEST(SchemaExtractor, ExtractsFromUnqualifiedFromButQualifiedJoin) {
  // FROM table (no schema) then JOIN shop.b → shop comes from the JOIN.
  EXPECT_EQ(SE::extract("SELECT * FROM a JOIN shop.b ON a.id=b.id"), "shop");
}

// ── Quoting ───────────────────────────────────────────────────────────────

TEST(SchemaExtractor, HandlesBacktickQuoting) {
  EXPECT_EQ(SE::extract("SELECT * FROM `billing`.`users`"), "billing");
  EXPECT_EQ(SE::extract("SELECT * FROM `my db`.`my table`"), "my db");
}

TEST(SchemaExtractor, HandlesDoubleQuoteQuoting) {
  EXPECT_EQ(SE::extract("SELECT * FROM \"billing\".\"users\""), "billing");
}

TEST(SchemaExtractor, HandlesMixedQuoting) {
  EXPECT_EQ(SE::extract("SELECT * FROM `billing`.users"), "billing");
  EXPECT_EQ(SE::extract("SELECT * FROM billing.`users`"), "billing");
}

// ── Whitespace around the dot ─────────────────────────────────────────────

TEST(SchemaExtractor, ToleratesSpacesAroundDot) {
  EXPECT_EQ(SE::extract("SELECT * FROM billing . users"), "billing");
}

// ── Case insensitivity of keywords ────────────────────────────────────────

TEST(SchemaExtractor, KeywordsAreCaseInsensitive) {
  EXPECT_EQ(SE::extract("select * from billing.users"), "billing");
  EXPECT_EQ(SE::extract("Select * From billing.users"), "billing");
}

// ── Things that must NOT be treated as schema ─────────────────────────────

TEST(SchemaExtractor, DoesNotMatchTableDotColumnInSelectList) {
  // `u.name` here is table.column, not schema.table; FROM has no schema.
  EXPECT_FALSE(SE::extract("SELECT u.name FROM users u").has_value());
}

TEST(SchemaExtractor, IgnoresKeywordInStringLiteral) {
  EXPECT_FALSE(SE::extract("SELECT 'from secret.table'").has_value());
  EXPECT_EQ(SE::extract("SELECT 'from x.y' FROM real.t"), "real");
}

TEST(SchemaExtractor, IgnoresFromSubstringInIdentifier) {
  // "fromage" contains "from" but is not the keyword.
  EXPECT_FALSE(SE::extract("SELECT fromage FROM cheeses").has_value());
}

// ── DDL ───────────────────────────────────────────────────────────────────

TEST(SchemaExtractor, ExtractsCreateTableSchema) {
  EXPECT_EQ(SE::extract("CREATE TABLE billing.invoices (id INT)"), "billing");
}

TEST(SchemaExtractor, ExtractsDropTableSchema) {
  EXPECT_EQ(SE::extract("DROP TABLE billing.invoices"), "billing");
}

// ── extract_or fallback ───────────────────────────────────────────────────

TEST(SchemaExtractor, ExtractOrUsesSqlSchemaWhenPresent) {
  EXPECT_EQ(SE::extract_or("SELECT * FROM billing.users", "session_db"),
            "billing");
}

TEST(SchemaExtractor, ExtractOrFallsBackToSessionSchema) {
  EXPECT_EQ(SE::extract_or("SELECT * FROM users", "session_db"), "session_db");
  EXPECT_EQ(SE::extract_or("SELECT 1", "app"), "app");
}

// ── Empty / weird input ───────────────────────────────────────────────────

TEST(SchemaExtractor, EmptyInputHasNoSchema) {
  EXPECT_FALSE(SE::extract("").has_value());
  EXPECT_FALSE(SE::extract("   ").has_value());
}

} // namespace dbmesh::routing
