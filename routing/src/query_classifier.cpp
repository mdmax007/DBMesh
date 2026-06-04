#include "dbmesh/routing/query_classifier.h"

#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbmesh::routing {

namespace {

// First keyword → QueryType. "with" is handled specially (CTE look-ahead).
const std::unordered_map<std::string_view, QueryType>& keyword_map() {
  static const std::unordered_map<std::string_view, QueryType> map = {
      {"select", QueryType::SELECT},   {"insert", QueryType::INSERT},
      {"update", QueryType::UPDATE},   {"delete", QueryType::DELETE},
      {"replace", QueryType::INSERT},  {"create", QueryType::DDL},
      {"drop", QueryType::DDL},        {"alter", QueryType::DDL},
      {"truncate", QueryType::DDL},    {"rename", QueryType::DDL},
      {"call", QueryType::CALL},       {"do", QueryType::CALL},
      {"begin", QueryType::BEGIN},     {"start", QueryType::BEGIN},
      {"commit", QueryType::COMMIT},   {"rollback", QueryType::ROLLBACK},
      {"set", QueryType::SET},         {"use", QueryType::USE},
      {"show", QueryType::SELECT},     {"explain", QueryType::SELECT},
      {"describe", QueryType::SELECT}, {"desc", QueryType::SELECT},
      {"analyze", QueryType::DDL},     {"optimize", QueryType::DDL},
      {"check", QueryType::DDL},       {"repair", QueryType::DDL},
      {"lock", QueryType::DDL},        {"unlock", QueryType::DDL},
      {"grant", QueryType::DDL},       {"revoke", QueryType::DDL},
      {"load", QueryType::INSERT},     {"values", QueryType::SELECT},
      {"table", QueryType::SELECT},  // bare TABLE t (MySQL 8)
  };
  return map;
}

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
bool is_word(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

// Removes -- , # and /* */ comments, preserving string/identifier literals.
std::string strip_comments(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  enum { NORMAL, SQUOTE, DQUOTE, BTICK } st = NORMAL;

  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    char n = (i + 1 < s.size()) ? s[i + 1] : '\0';

    if (st == NORMAL) {
      if (c == '-' && n == '-') {
        while (i < s.size() && s[i] != '\n') ++i;
        out += ' ';
        continue;
      }
      if (c == '#') {
        while (i < s.size() && s[i] != '\n') ++i;
        out += ' ';
        continue;
      }
      if (c == '/' && n == '*') {
        i += 2;
        while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
        ++i;  // land on '/', loop's ++i skips it
        out += ' ';
        continue;
      }
      if (c == '\'') st = SQUOTE;
      else if (c == '"') st = DQUOTE;
      else if (c == '`') st = BTICK;
      out += c;
    } else {
      out += c;
      char q = (st == SQUOTE) ? '\'' : (st == DQUOTE) ? '"' : '`';
      if (c == '\\' && st != BTICK) {  // backslash escape (not in backticks)
        if (n != '\0') { out += n; ++i; }
        continue;
      }
      if (c == q) {
        if (n == q) { out += n; ++i; continue; }  // doubled-quote escape
        st = NORMAL;
      }
    }
  }
  return out;
}

// Splits on top-level ';' (outside string/identifier literals).
std::vector<std::string_view> split_statements(std::string_view s) {
  std::vector<std::string_view> out;
  enum { NORMAL, SQUOTE, DQUOTE, BTICK } st = NORMAL;
  std::size_t start = 0;

  auto push = [&](std::string_view sv) {
    std::size_t a = 0, b = sv.size();
    while (a < b && is_space(sv[a])) ++a;
    while (b > a && is_space(sv[b - 1])) --b;
    if (a < b) out.push_back(sv.substr(a, b - a));
  };

  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (st == NORMAL) {
      if (c == '\'') st = SQUOTE;
      else if (c == '"') st = DQUOTE;
      else if (c == '`') st = BTICK;
      else if (c == ';') { push(s.substr(start, i - start)); start = i + 1; }
    } else {
      char q = (st == SQUOTE) ? '\'' : (st == DQUOTE) ? '"' : '`';
      if (c == '\\' && st != BTICK) { ++i; }
      else if (c == q) st = NORMAL;
    }
  }
  push(s.substr(start));
  return out;
}

// Reads the leading keyword (lowercased) after skipping whitespace and '('.
std::string_view first_keyword(std::string_view s, std::array<char, 16>& buf) {
  std::size_t i = 0;
  while (i < s.size() && (is_space(s[i]) || s[i] == '(')) ++i;
  std::size_t n = 0;
  while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) {
    if (n >= buf.size()) return {};  // too long to be a known keyword
    buf[n++] = lower(s[i++]);
  }
  return {buf.data(), n};
}

// For a CTE (WITH ...), find the main statement keyword at paren depth 0.
QueryType classify_cte(std::string_view s) {
  enum { NORMAL, SQUOTE, DQUOTE, BTICK } st = NORMAL;
  int depth = 0;
  std::size_t i = 0;

  while (i < s.size()) {
    char c = s[i];
    if (st != NORMAL) {
      char q = (st == SQUOTE) ? '\'' : (st == DQUOTE) ? '"' : '`';
      if (c == '\\' && st != BTICK) ++i;
      else if (c == q) st = NORMAL;
      ++i;
      continue;
    }
    if (c == '\'') { st = SQUOTE; ++i; continue; }
    if (c == '"')  { st = DQUOTE; ++i; continue; }
    if (c == '`')  { st = BTICK;  ++i; continue; }
    if (c == '(')  { ++depth; ++i; continue; }
    if (c == ')')  { if (depth > 0) --depth; ++i; continue; }

    if (depth == 0 && std::isalpha(static_cast<unsigned char>(c))) {
      std::array<char, 16> kw{};
      std::size_t n = 0;
      while (i < s.size() && is_word(s[i])) {
        if (n < kw.size()) kw[n] = lower(s[i]);
        ++n;
        ++i;
      }
      std::string_view word{kw.data(), std::min(n, kw.size())};
      if (n <= kw.size()) {
        if (word == "select") return QueryType::SELECT;
        if (word == "insert" || word == "replace") return QueryType::INSERT;
        if (word == "update" || word == "merge") return QueryType::UPDATE;
        if (word == "delete") return QueryType::DELETE;
      }
      continue;
    }
    ++i;
  }
  return QueryType::SELECT;  // CTEs default to read
}

} // namespace

QueryType QueryClassifier::classify_one(std::string_view stmt) {
  std::array<char, 16> buf{};
  std::string_view kw = first_keyword(stmt, buf);
  if (kw.empty()) return QueryType::UNKNOWN;

  if (kw == "with") {
    // Skip leading whitespace/'(' so the CTE scanner starts at 'with'.
    std::size_t i = 0;
    while (i < stmt.size() && (is_space(stmt[i]) || stmt[i] == '(')) ++i;
    return classify_cte(stmt.substr(i));
  }

  const auto& map = keyword_map();
  auto it = map.find(kw);
  return it == map.end() ? QueryType::UNKNOWN : it->second;
}

QueryType QueryClassifier::classify(std::string_view sql) {
  std::string cleaned = strip_comments(sql);
  auto stmts = split_statements(cleaned);

  if (stmts.empty()) return QueryType::UNKNOWN;
  if (stmts.size() == 1) return classify_one(stmts[0]);

  // Multi-statement: escalate to the first write/DDL; else the first known type.
  QueryType first_known = QueryType::UNKNOWN;
  bool have = false;
  for (auto st : stmts) {
    QueryType t = classify_one(st);
    if (t == QueryType::UNKNOWN) continue;
    if (!have) { first_known = t; have = true; }
    if (is_write(t)) return t;  // is_write covers INSERT/UPDATE/DELETE/DDL/CALL
  }
  return have ? first_known : QueryType::UNKNOWN;
}

} // namespace dbmesh::routing
