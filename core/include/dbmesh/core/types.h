#ifndef DBMESH_CORE_TYPES_H_
#define DBMESH_CORE_TYPES_H_

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_hash.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <cstdint>
#include <string>

namespace dbmesh {

// ── Opaque string identifiers ────────────────────────────────────────────
using BackendID  = std::string;
using SchemaName = std::string;
using NodeID     = std::string;
using TenantID   = std::string;

// ── Session identifier (UUID v4, stable across connection lifetime) ───────
struct SessionID {
  boost::uuids::uuid value{};

  [[nodiscard]] static SessionID generate() {
    // thread_local avoids mutex on the generator
    static thread_local boost::uuids::random_generator gen{};
    return SessionID{gen()};
  }

  [[nodiscard]] std::string to_string() const {
    return boost::uuids::to_string(value);
  }

  auto operator<=>(const SessionID&) const = default;
  bool operator==(const SessionID&) const  = default;

  struct Hash {
    std::size_t operator()(const SessionID& id) const noexcept {
      return boost::hash<boost::uuids::uuid>{}(id.value);
    }
  };
};

// ── Query classification ─────────────────────────────────────────────────
enum class QueryType : uint8_t {
  SELECT,
  INSERT,
  UPDATE,
  DELETE,
  DDL,
  CALL,
  BEGIN,
  COMMIT,
  ROLLBACK,
  SET,
  USE,
  UNKNOWN,
};

[[nodiscard]] constexpr bool is_read(QueryType t) noexcept {
  return t == QueryType::SELECT;
}

[[nodiscard]] constexpr bool is_write(QueryType t) noexcept {
  return t == QueryType::INSERT || t == QueryType::UPDATE ||
         t == QueryType::DELETE || t == QueryType::DDL  ||
         t == QueryType::CALL;
}

// ── Backend ──────────────────────────────────────────────────────────────
enum class BackendType : uint8_t {
  MYSQL,
  MARIADB,
  POSTGRES,
  RDS_MYSQL,
  RDS_MARIADB,
  RDS_POSTGRES,
};

enum class BackendRole : uint8_t { PRIMARY, REPLICA };

enum class BackendState : uint8_t {
  HEALTHY,
  DEGRADED,
  FAILED,
  RECOVERING,
  DRAINING,
};

// ── Session policy ────────────────────────────────────────────────────────
enum class RecoveryMode : uint8_t { STATELESS, BASIC, ADVANCED, STRICT };

enum class AffinityMode : uint8_t { NONE, SCHEMA, BACKEND, TRANSACTION };

enum class RoutingPolicy : uint8_t {
  ROUND_ROBIN,
  WEIGHTED,
  LEAST_CONNECTIONS,
  LATENCY,
  RANDOM,
  FAILOVER,
  CONSISTENT_HASHING,
};

// ── Cluster / failover ────────────────────────────────────────────────────
enum class NodeMode : uint8_t { STANDALONE, CLUSTER };

enum class FailoverPolicy : uint8_t { IMMEDIATE, GRACEFUL, MANUAL };

} // namespace dbmesh

#endif // DBMESH_CORE_TYPES_H_
