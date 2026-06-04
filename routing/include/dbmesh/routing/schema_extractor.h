#ifndef DBMESH_ROUTING_SCHEMA_EXTRACTOR_H_
#define DBMESH_ROUTING_SCHEMA_EXTRACTOR_H_

#include "dbmesh/core/types.h"

#include <optional>
#include <string_view>

namespace dbmesh::routing {

// Extracts the target schema (database) from a SQL statement using a hand-
// written scanner — no regex (architecture.md §6). Looks for `database.table`
// qualified names after FROM / INTO / UPDATE / JOIN / TABLE. Handles backtick
// and double-quote quoting.
//
// Returns nullopt when no schema is determinable from the SQL; the caller then
// falls back to the session's current schema.
class SchemaExtractor {
 public:
  [[nodiscard]] static std::optional<SchemaName> extract(std::string_view sql);

  // Convenience: extract from SQL, falling back to `session_schema` when the
  // SQL has no explicit schema qualifier.
  [[nodiscard]] static SchemaName extract_or(std::string_view sql,
                                             std::string_view session_schema);
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_SCHEMA_EXTRACTOR_H_
