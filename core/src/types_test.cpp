#include "dbmesh/core/result.h"
#include "dbmesh/core/types.h"

#include <gtest/gtest.h>

#include <unordered_set>

namespace dbmesh {

// ── Result<T,E> ───────────────────────────────────────────────────────────

TEST(Result, OkHoldsValue) {
  Result<int, std::string> r = Ok(42);
  EXPECT_TRUE(is_ok(r));
  EXPECT_FALSE(is_err(r));
  EXPECT_EQ(get_value(r), 42);
}

TEST(Result, ErrHoldsError) {
  Result<int, std::string> r = Err(std::string("boom"));
  EXPECT_FALSE(is_ok(r));
  EXPECT_TRUE(is_err(r));
  EXPECT_EQ(get_error(r), "boom");
}

TEST(Result, GetIfWorksOnVariant) {
  Result<int, std::string> ok  = Ok(7);
  Result<int, std::string> err = Err(std::string("oops"));

  EXPECT_NE(std::get_if<int>(&ok), nullptr);
  EXPECT_EQ(std::get_if<std::string>(&ok), nullptr);

  EXPECT_EQ(std::get_if<int>(&err), nullptr);
  EXPECT_NE(std::get_if<std::string>(&err), nullptr);
}

TEST(Result, MoveSemantics) {
  Result<std::string, int> r = Ok(std::string("hello"));
  EXPECT_EQ(get_value(r), "hello");
  std::string moved = std::move(get_value(r));
  EXPECT_EQ(moved, "hello");
}

// ── QueryType helpers ─────────────────────────────────────────────────────

TEST(QueryType, IsRead) {
  EXPECT_TRUE(is_read(QueryType::SELECT));
  EXPECT_FALSE(is_read(QueryType::INSERT));
  EXPECT_FALSE(is_read(QueryType::UPDATE));
  EXPECT_FALSE(is_read(QueryType::DELETE));
  EXPECT_FALSE(is_read(QueryType::DDL));
}

TEST(QueryType, IsWrite) {
  EXPECT_TRUE(is_write(QueryType::INSERT));
  EXPECT_TRUE(is_write(QueryType::UPDATE));
  EXPECT_TRUE(is_write(QueryType::DELETE));
  EXPECT_TRUE(is_write(QueryType::DDL));
  EXPECT_TRUE(is_write(QueryType::CALL));
  EXPECT_FALSE(is_write(QueryType::SELECT));
  EXPECT_FALSE(is_write(QueryType::BEGIN));
}

// ── SessionID ─────────────────────────────────────────────────────────────

TEST(SessionID, GeneratesUniqueIds) {
  constexpr int N = 1000;
  std::unordered_set<std::string> seen;
  for (int i = 0; i < N; ++i) {
    auto id = SessionID::generate();
    EXPECT_TRUE(seen.insert(id.to_string()).second)
        << "duplicate session ID generated";
  }
}

TEST(SessionID, ToStringIsUuidFormat) {
  auto id  = SessionID::generate();
  auto str = id.to_string();
  // xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  EXPECT_EQ(str.size(), 36u);
  EXPECT_EQ(str[8],  '-');
  EXPECT_EQ(str[13], '-');
  EXPECT_EQ(str[18], '-');
  EXPECT_EQ(str[23], '-');
}

TEST(SessionID, EqualityAndOrdering) {
  auto a = SessionID::generate();
  auto b = SessionID::generate();
  EXPECT_EQ(a, a);
  EXPECT_NE(a, b);
}

TEST(SessionID, UsableAsMapKey) {
  std::unordered_map<SessionID, int, SessionID::Hash> m;
  auto id = SessionID::generate();
  m[id]   = 42;
  EXPECT_EQ(m.at(id), 42);
}

} // namespace dbmesh
