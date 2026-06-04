#include "dbmesh/routing/schema_extractor.h"

#include <array>
#include <cctype>
#include <string>

namespace dbmesh::routing {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
bool is_ident(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

// Reads one identifier starting at *pos (bare, `backtick`, or "double-quoted").
// Advances *pos. Returns the identifier text (unquoted), empty if none.
std::string read_ident(std::string_view s, std::size_t* pos) {
  std::size_t i = *pos;
  while (i < s.size() && is_space(s[i])) ++i;
  if (i >= s.size()) { *pos = i; return {}; }

  std::string out;
  if (s[i] == '`' || s[i] == '"') {
    char q = s[i++];
    while (i < s.size()) {
      if (s[i] == q) {
        if (i + 1 < s.size() && s[i + 1] == q) { out += q; i += 2; continue; }
        ++i;
        break;
      }
      out += s[i++];
    }
  } else {
    while (i < s.size() && is_ident(s[i])) out += s[i++];
  }
  *pos = i;
  return out;
}

// At *pos, read a (possibly schema-qualified) name. If it is `schema.table`,
// returns the schema; otherwise returns empty. Advances *pos past the name.
std::string read_qualified_schema(std::string_view s, std::size_t* pos) {
  std::string first = read_ident(s, pos);
  if (first.empty()) return {};

  std::size_t i = *pos;
  while (i < s.size() && is_space(s[i])) ++i;
  if (i < s.size() && s[i] == '.') {
    ++i;  // consume dot
    *pos = i;
    std::string second = read_ident(s, pos);
    if (!second.empty()) return first;  // schema.table → schema
  }
  return {};
}

// Case-insensitive word match of `kw` at s[i..], requiring a word boundary
// before (handled by caller) and after.
bool word_at(std::string_view s, std::size_t i, std::string_view kw) {
  if (i + kw.size() > s.size()) return false;
  for (std::size_t k = 0; k < kw.size(); ++k)
    if (lower(s[i + k]) != kw[k]) return false;
  std::size_t after = i + kw.size();
  return after >= s.size() || !is_ident(s[after]);
}

} // namespace

std::optional<SchemaName> SchemaExtractor::extract(std::string_view s) {
  enum { NORMAL, SQUOTE, DQUOTE, BTICK } st = NORMAL;

  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];

    if (st != NORMAL) {
      char q = (st == SQUOTE) ? '\'' : (st == DQUOTE) ? '"' : '`';
      if (c == '\\' && st != BTICK) ++i;
      else if (c == q) st = NORMAL;
      continue;
    }
    if (c == '\'') { st = SQUOTE; continue; }
    // Note: " and ` may begin a quoted identifier we want to read, so only
    // enter quote-skip state for single quotes (string literals). Quoted
    // identifiers are consumed by read_ident below.

    // Word-boundary check: previous char must be a non-identifier.
    bool boundary = (i == 0) || !is_ident(s[i - 1]);
    if (!boundary || std::isalpha(static_cast<unsigned char>(c)) == 0) continue;

    std::size_t after = 0;
    bool matched = false;
    for (std::string_view kw : {"from", "into", "update", "join", "table"}) {
      if (word_at(s, i, kw)) { after = i + kw.size(); matched = true; break; }
    }
    if (!matched) continue;

    std::size_t pos = after;
    std::string schema = read_qualified_schema(s, &pos);
    if (!schema.empty()) return schema;
    // Keep scanning from just past the keyword (pos may have advanced).
    i = (pos > i) ? pos - 1 : i;
  }
  return std::nullopt;
}

SchemaName SchemaExtractor::extract_or(std::string_view sql,
                                       std::string_view session_schema) {
  auto s = extract(sql);
  return s.has_value() ? *s : SchemaName(session_schema);
}

} // namespace dbmesh::routing
