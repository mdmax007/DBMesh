#ifndef DBMESH_CORE_CONFIG_H_
#define DBMESH_CORE_CONFIG_H_

#include "dbmesh/core/result.h"
#include "dbmesh/core/types.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dbmesh {

// ── Node ─────────────────────────────────────────────────────────────────

struct NodeConfig {
  std::string id;
  std::string name;
  std::string data_dir = "/var/lib/dbmesh";
  NodeMode    mode     = NodeMode::STANDALONE;
};

// ── Listeners ─────────────────────────────────────────────────────────────

struct ListenerConfig {
  bool        enabled         = false;
  std::string bind            = "0.0.0.0";
  uint16_t    port            = 0;
  uint32_t    max_connections = 100000;
  uint32_t    backlog         = 1024;
  uint32_t    io_threads      = 4;
};

struct ListenersConfig {
  ListenerConfig mysql;
  ListenerConfig postgres;
  ListenerConfig admin;
};

// ── TLS ───────────────────────────────────────────────────────────────────

struct TlsEndpointConfig {
  bool        enabled     = false;
  std::string cert;
  std::string key;
  std::string ca;
  bool        mutual      = false;
  std::string min_version = "TLSv1.2";
};

struct TlsConfig {
  TlsEndpointConfig frontend;
  struct BackendTls : TlsEndpointConfig {
    bool verify_server = true;
  } backend;
};

// ── Connection pool ───────────────────────────────────────────────────────

struct PoolConfig {
  uint32_t min                = 10;
  uint32_t max                = 100;
  uint32_t idle_timeout_sec   = 60;
  uint32_t connect_timeout_ms = 2000;
  uint32_t acquire_timeout_ms = 5000;
  bool     warm               = true;
};

// ── Backends ──────────────────────────────────────────────────────────────

struct BackendConfig {
  std::string                        id;
  BackendType                        type  = BackendType::MYSQL;
  std::string                        host;
  uint16_t                           port  = 3306;
  BackendRole                        role  = BackendRole::PRIMARY;
  std::string                        group;
  std::string                        user;
  std::string                        password;
  std::string                        secrets_file;
  uint32_t                           weight                  = 100;
  uint32_t                           max_replication_lag_ms  = 500;
  PoolConfig                         pool;
  std::map<std::string, std::string> tags;
};

// ── Schema registry ───────────────────────────────────────────────────────

struct SchemaConfig {
  std::string                   name;
  std::string                   backend_group;
  std::optional<RoutingPolicy>  routing_policy;
  std::optional<RecoveryMode>   session_recovery;
  std::optional<AffinityMode>   session_affinity;
  std::optional<bool>           read_write_split;
};

// ── Routing ───────────────────────────────────────────────────────────────

struct TenantMapping {
  std::string tenant;
  std::string backend_group;
};

struct RoutingConfig {
  RoutingPolicy default_policy          = RoutingPolicy::ROUND_ROBIN;
  bool          read_write_split        = true;
  bool          send_writes_to_primary  = true;
  bool          replication_aware       = true;
  uint32_t      max_replica_lag_ms      = 500;
  struct {
    bool                      enabled       = false;
    std::string               attribute_key = "tenant";
    std::vector<TenantMapping> mappings;
  } tenants;
};

// ── Session ───────────────────────────────────────────────────────────────

struct PerUserSessionConfig {
  std::string                  user;
  std::optional<RecoveryMode>  recovery_mode;
  std::optional<AffinityMode>  affinity;
};

struct SessionConfig {
  RecoveryMode recovery_mode   = RecoveryMode::BASIC;
  AffinityMode affinity        = AffinityMode::TRANSACTION;
  uint32_t     max_idle_sec    = 300;
  uint32_t     max_lifetime_sec = 3600;
  std::vector<PerUserSessionConfig> per_user;
};

// ── Failover ──────────────────────────────────────────────────────────────

struct HealthCheckConfig {
  uint32_t    interval_ms        = 100;
  uint32_t    timeout_ms         = 500;
  uint32_t    failure_threshold  = 3;
  uint32_t    recovery_threshold = 2;
  std::string method             = "query";
  std::string query              = "SELECT 1";
};

struct QueryRecoveryConfig {
  bool        enabled       = true;
  bool        retry_selects = true;
  bool        retry_writes  = false;
  uint32_t    max_retries   = 3;
  uint32_t    retry_delay_ms = 100;
  std::string retry_backoff = "exponential";
};

struct FailoverConfig {
  bool               enabled             = true;
  FailoverPolicy     policy              = FailoverPolicy::IMMEDIATE;
  uint32_t           grace_period_ms     = 5000;
  bool               promotion_enabled   = true;
  std::string        promotion_strategy  = "most_recent";
  QueryRecoveryConfig query_recovery;
  HealthCheckConfig   health_check;
};

// ── Cluster / gossip ──────────────────────────────────────────────────────

struct GossipConfig {
  uint32_t                 interval_ms      = 200;
  uint32_t                 fanout           = 3;
  uint32_t                 suspicion_mult   = 4;
  uint32_t                 retransmit_mult  = 4;
  std::vector<std::string> sync_keys = {
    "schema_registry", "backend_state", "membership",
    "health", "config_versions"};
};

struct ClusterConfig {
  bool                     enabled     = false;
  std::string              bind        = "0.0.0.0";
  uint16_t                 port        = 7946;
  std::string              discovery   = "hybrid";
  std::vector<std::string> seed_nodes;
  std::string              seed_dns;
  GossipConfig             gossip;
};

// ── Auth ──────────────────────────────────────────────────────────────────

struct LdapConfig {
  bool        enabled         = false;
  std::string url;
  std::string base_dn;
  std::string bind_dn;
  std::string bind_password;
  std::string user_filter     = "(uid={user})";
  std::string group_attribute = "memberOf";
  uint32_t    timeout_ms      = 3000;
  bool        start_tls       = false;
};

struct JwtConfig {
  bool        enabled    = false;
  std::string algorithm  = "RS256";
  std::string public_key;
  std::string secret;
  std::string issuer;
  std::string audience;
  uint32_t    leeway_sec = 30;
};

struct AuthConfig {
  std::string method = "local";
  struct {
    std::string users_file = "/etc/dbmesh/users.yaml";
  } local;
  LdapConfig ldap;
  JwtConfig  jwt;
};

struct RbacConfig {
  bool        enabled    = true;
  std::string roles_file = "/etc/dbmesh/roles.yaml";
};

// ── Query firewall ────────────────────────────────────────────────────────

struct FirewallRule {
  std::string              name;
  std::string              match;        // substring match (case-insensitive)
  std::string              match_regex;  // regex match (compiled at startup)
  std::string              action;       // block | warn | log
  std::string              message;
  std::vector<std::string> schemas;
  std::vector<std::string> users;
};

struct FirewallConfig {
  bool                     enabled        = false;
  std::string              default_action = "allow";
  std::vector<FirewallRule> rules;
};

// ── Rate limiting ─────────────────────────────────────────────────────────

struct RateLimitOverride {
  std::string key;  // username, schema name, IP, or tenant
  uint32_t    qps = 0;
};

struct RateLimitDimension {
  bool         enabled          = false;
  uint32_t     default_qps      = 5000;
  double       burst_multiplier = 2.0;
  std::vector<RateLimitOverride> overrides;
};

struct RateLimitConfig {
  bool               enabled    = false;
  RateLimitDimension per_user;
  RateLimitDimension per_ip;
  RateLimitDimension per_schema;
  RateLimitDimension per_tenant;
};

// ── Management API ────────────────────────────────────────────────────────

struct ApiConfig {
  bool        enabled               = true;
  std::string bind                  = "127.0.0.1";
  uint16_t    port                  = 8080;
  bool        auth                  = true;
  std::string token_file            = "/etc/dbmesh/api_token";
  struct {
    bool        enabled = true;
    std::string path    = "/ui";
  } ui;
  struct {
    bool                     enabled = false;
    std::vector<std::string> origins;
  } cors;
  uint32_t max_request_body_kb = 512;
  uint32_t request_timeout_ms  = 10000;
};

// ── Metrics / observability ───────────────────────────────────────────────

struct PrometheusConfig {
  bool        enabled = true;
  std::string bind    = "0.0.0.0";
  uint16_t    port    = 9090;
  std::string path    = "/metrics";
  bool        auth    = false;
};

struct TracingConfig {
  bool        enabled         = false;
  std::string exporter        = "otlp";
  std::string endpoint        = "http://otel-collector:4318";
  std::string service_name    = "dbmesh";
  std::string service_version = "0.1.0";
  double      sample_rate     = 0.1;
  std::string propagation     = "w3c";
};

struct MetricsConfig {
  PrometheusConfig prometheus;
  TracingConfig    tracing;
};

// ── Logging ───────────────────────────────────────────────────────────────

struct LogFileConfig {
  std::string path        = "/var/log/dbmesh/dbmesh.log";
  bool        rotate      = true;
  uint32_t    max_size_mb = 100;
  uint32_t    max_backups = 7;
  bool        compress    = true;
};

struct AuditConfig {
  bool        enabled             = true;
  std::string output              = "both";
  std::string file                = "/var/log/dbmesh/audit.log";
  bool        log_queries         = false;
  bool        log_connections     = true;
  bool        log_failovers       = true;
  bool        log_auth            = true;
  bool        log_config_changes  = true;
  bool        log_firewall_hits   = true;
};

struct LoggingConfig {
  std::string   level  = "INFO";
  std::string   format = "json";
  std::string   output = "stdout";
  LogFileConfig file;
  AuditConfig   audit;
};

// ── Root config ───────────────────────────────────────────────────────────

struct Config {
  NodeConfig                node;
  ListenersConfig           listeners;
  TlsConfig                 tls;
  std::vector<BackendConfig> backends;
  std::vector<SchemaConfig>  schemas;
  RoutingConfig             routing;
  SessionConfig             session;
  FailoverConfig            failover;
  ClusterConfig             cluster;
  AuthConfig                auth;
  RbacConfig                rbac;
  FirewallConfig            firewall;
  RateLimitConfig           rate_limit;
  ApiConfig                 api;
  MetricsConfig             metrics;
  LoggingConfig             logging;
};

// ── ConfigLoader ──────────────────────────────────────────────────────────

class ConfigLoader {
 public:
  [[nodiscard]] static Result<Config, std::string> load(const std::string& path);
  [[nodiscard]] static std::optional<std::string>  validate(const Config& config);
};

// ── ConfigReloader ────────────────────────────────────────────────────────
// Triggered by SIGHUP or POST /api/v1/config/reload.
// Atomically swaps the config pointer; callers observe the new config
// on their next access.

class ConfigReloader {
 public:
  using Callback = std::function<void(std::shared_ptr<const Config>)>;

  ConfigReloader(std::string path, Callback on_reload);

  // Called from signal handler context — must be safe to call concurrently.
  void reload();

 private:
  std::string path_;
  Callback    on_reload_;
};

} // namespace dbmesh

#endif // DBMESH_CORE_CONFIG_H_
