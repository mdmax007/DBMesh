#ifndef DBMESH_ROUTING_QUERY_CLASSIFIER_H_
#define DBMESH_ROUTING_QUERY_CLASSIFIER_H_

#include "dbmesh/core/types.h"

#include <string_view>

namespace dbmesh::routing {

// Classifies a SQL statement by its first keyword. No full parse, no regex —
// a keyword lookup table plus a hand-written scanner for comments, multi-
// statement batches, and CTEs (see architecture.md §6).
//
// All methods are pure and thread-safe.
class QueryClassifier {
 public:
  // Classifies a (possibly multi-statement) SQL string. For a batch, the
  // result is escalated to the most dangerous type — any write/DDL makes the
  // whole batch route as a write.
  [[nodiscard]] static QueryType classify(std::string_view sql);

  // Classifies a single statement (no ';' splitting). Comments are tolerated.
  [[nodiscard]] static QueryType classify_one(std::string_view stmt);
};

} // namespace dbmesh::routing

#endif // DBMESH_ROUTING_QUERY_CLASSIFIER_H_
