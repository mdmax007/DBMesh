#include "dbmesh/routing/routing_engine.h"

#include "dbmesh/routing/query_classifier.h"
#include "dbmesh/routing/read_write_router.h"
#include "dbmesh/routing/schema_extractor.h"

namespace dbmesh::routing {

RoutingEngine::RoutingEngine(const SchemaRegistry& schemas,
                              const BackendSelector& selector,
                              RoutingConfig routing)
    : schemas_(schemas), selector_(selector), routing_(routing) {}

Result<RoutePlan, RoutingError> RoutingEngine::plan(
    std::string_view sql, const SessionView& session,
    const RoutingContext& ctx) const {
  // Step 1: classify.
  QueryType type = QueryClassifier::classify(sql);

  // Step 2: extract schema (fall back to the session's current schema).
  SchemaName schema = SchemaExtractor::extract_or(sql, session.current_schema);

  // Step 3: tenant resolution — Milestone 1.5 leaves this as a pass-through.

  // Step 4: schema registry lookup (→ default → error).
  const SchemaConfig* config = schemas_.lookup(schema);
  if (config == nullptr) return Err(RoutingError::NO_SCHEMA_CONFIG);

  // Step 6: read/write role.
  RwSessionState rw;
  rw.in_transaction = session.in_transaction;
  rw.autocommit     = session.autocommit;
  BackendRole role  = ReadWriteRouter::select_role(type, rw);

  // Per-schema (or global) read/write split: when disabled, everything goes to
  // the primary.
  bool rw_split = config->read_write_split.value_or(routing_.read_write_split);
  if (!rw_split) role = BackendRole::PRIMARY;

  // Step 5: affinity / pinning. If the session is pinned to a backend (and it
  // still serves this schema's group), honour it. Full AffinityEngine is M1.6;
  // here we respect an explicit pin when present.
  if (session.pinned_backend.has_value() && role == BackendRole::PRIMARY) {
    // Only short-circuit for primary-bound work; replica reads still load
    // balance. (Transaction pinning sets in_transaction → PRIMARY above.)
    return Ok(RoutePlan{type, schema, *session.pinned_backend, role});
  }

  // Step 7: backend selection.
  auto backend = selector_.select(*config, role, ctx);
  if (is_err(backend)) return Err(get_error(backend));

  // Steps 8–9: firewall + rate limit (Milestone 1.10) — allow-all for now.

  return Ok(RoutePlan{type, schema, get_value(backend), role});
}

} // namespace dbmesh::routing
