#include "dbmesh/core/config.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>

namespace dbmesh {

namespace {

// ── Enum parsers ──────────────────────────────────────────────────────────

NodeMode parse_node_mode(const std::string& s) {
  if (s == "standalone") return NodeMode::STANDALONE;
  if (s == "cluster")    return NodeMode::CLUSTER;
  throw std::runtime_error("invalid node.mode '" + s + "' (standalone|cluster)");
}

BackendType parse_backend_type(const std::string& s) {
  if (s == "mysql")       return BackendType::MYSQL;
  if (s == "mariadb")     return BackendType::MARIADB;
  if (s == "postgres")    return BackendType::POSTGRES;
  if (s == "rds-mysql")   return BackendType::RDS_MYSQL;
  if (s == "rds-mariadb") return BackendType::RDS_MARIADB;
  if (s == "rds-postgres") return BackendType::RDS_POSTGRES;
  throw std::runtime_error("invalid backend type '" + s + "'");
}

BackendRole parse_backend_role(const std::string& s) {
  if (s == "primary") return BackendRole::PRIMARY;
  if (s == "replica") return BackendRole::REPLICA;
  throw std::runtime_error("invalid role '" + s + "' (primary|replica)");
}

RecoveryMode parse_recovery_mode(const std::string& s) {
  if (s == "STATELESS") return RecoveryMode::STATELESS;
  if (s == "BASIC")     return RecoveryMode::BASIC;
  if (s == "ADVANCED")  return RecoveryMode::ADVANCED;
  if (s == "STRICT")    return RecoveryMode::STRICT;
  throw std::runtime_error("invalid recovery_mode '" + s +
                           "' (STATELESS|BASIC|ADVANCED|STRICT)");
}

AffinityMode parse_affinity_mode(const std::string& s) {
  if (s == "NONE")        return AffinityMode::NONE;
  if (s == "SCHEMA")      return AffinityMode::SCHEMA;
  if (s == "BACKEND")     return AffinityMode::BACKEND;
  if (s == "TRANSACTION") return AffinityMode::TRANSACTION;
  throw std::runtime_error("invalid affinity '" + s +
                           "' (NONE|SCHEMA|BACKEND|TRANSACTION)");
}

RoutingPolicy parse_routing_policy(const std::string& s) {
  if (s == "round_robin")        return RoutingPolicy::ROUND_ROBIN;
  if (s == "weighted")           return RoutingPolicy::WEIGHTED;
  if (s == "least_connections")  return RoutingPolicy::LEAST_CONNECTIONS;
  if (s == "latency")            return RoutingPolicy::LATENCY;
  if (s == "random")             return RoutingPolicy::RANDOM;
  if (s == "failover")           return RoutingPolicy::FAILOVER;
  if (s == "consistent_hashing") return RoutingPolicy::CONSISTENT_HASHING;
  throw std::runtime_error("invalid routing_policy '" + s + "'");
}

FailoverPolicy parse_failover_policy(const std::string& s) {
  if (s == "IMMEDIATE") return FailoverPolicy::IMMEDIATE;
  if (s == "GRACEFUL")  return FailoverPolicy::GRACEFUL;
  if (s == "MANUAL")    return FailoverPolicy::MANUAL;
  throw std::runtime_error("invalid failover.policy '" + s +
                           "' (IMMEDIATE|GRACEFUL|MANUAL)");
}

// ── Section parsers ───────────────────────────────────────────────────────

PoolConfig parse_pool(const YAML::Node& n) {
  PoolConfig p;
  if (!n) return p;
  if (n["min"])                p.min                = n["min"].as<uint32_t>();
  if (n["max"])                p.max                = n["max"].as<uint32_t>();
  if (n["idle_timeout_sec"])   p.idle_timeout_sec   = n["idle_timeout_sec"].as<uint32_t>();
  if (n["connect_timeout_ms"]) p.connect_timeout_ms = n["connect_timeout_ms"].as<uint32_t>();
  if (n["acquire_timeout_ms"]) p.acquire_timeout_ms = n["acquire_timeout_ms"].as<uint32_t>();
  if (n["warm"])               p.warm               = n["warm"].as<bool>();
  return p;
}

ListenerConfig parse_listener(const YAML::Node& n, uint16_t default_port) {
  ListenerConfig l;
  l.port = default_port;
  if (!n) return l;
  if (n["enabled"])         l.enabled         = n["enabled"].as<bool>();
  if (n["bind"])            l.bind            = n["bind"].as<std::string>();
  if (n["port"])            l.port            = static_cast<uint16_t>(n["port"].as<uint32_t>());
  if (n["max_connections"]) l.max_connections = n["max_connections"].as<uint32_t>();
  if (n["backlog"])         l.backlog         = n["backlog"].as<uint32_t>();
  if (n["io_threads"])      l.io_threads      = n["io_threads"].as<uint32_t>();
  return l;
}

TlsEndpointConfig parse_tls_endpoint(const YAML::Node& n) {
  TlsEndpointConfig t;
  if (!n) return t;
  if (n["enabled"])     t.enabled     = n["enabled"].as<bool>();
  if (n["cert"])        t.cert        = n["cert"].as<std::string>();
  if (n["key"])         t.key         = n["key"].as<std::string>();
  if (n["ca"])          t.ca          = n["ca"].as<std::string>();
  if (n["mutual"])      t.mutual      = n["mutual"].as<bool>();
  if (n["min_version"]) t.min_version = n["min_version"].as<std::string>();
  return t;
}

BackendConfig parse_backend(const YAML::Node& n) {
  BackendConfig b;
  if (n["id"])   b.id   = n["id"].as<std::string>();
  if (n["type"]) b.type = parse_backend_type(n["type"].as<std::string>());
  if (n["host"]) b.host = n["host"].as<std::string>();
  if (n["port"]) b.port = static_cast<uint16_t>(n["port"].as<uint32_t>());
  if (n["role"]) b.role = parse_backend_role(n["role"].as<std::string>());
  if (n["group"]) b.group = n["group"].as<std::string>();
  if (n["user"]) b.user = n["user"].as<std::string>();
  if (n["password"])    b.password    = n["password"].as<std::string>();
  if (n["secrets_file"]) b.secrets_file = n["secrets_file"].as<std::string>();
  if (n["weight"])      b.weight      = n["weight"].as<uint32_t>();
  if (n["max_replication_lag_ms"])
    b.max_replication_lag_ms = n["max_replication_lag_ms"].as<uint32_t>();
  if (n["pool"]) b.pool = parse_pool(n["pool"]);
  if (n["tags"] && n["tags"].IsMap()) {
    for (const auto& kv : n["tags"])
      b.tags[kv.first.as<std::string>()] = kv.second.as<std::string>();
  }
  return b;
}

SchemaConfig parse_schema(const YAML::Node& n) {
  SchemaConfig s;
  if (n["name"])          s.name          = n["name"].as<std::string>();
  if (n["backend_group"]) s.backend_group = n["backend_group"].as<std::string>();
  if (n["routing_policy"])
    s.routing_policy = parse_routing_policy(n["routing_policy"].as<std::string>());
  if (n["session_recovery"])
    s.session_recovery = parse_recovery_mode(n["session_recovery"].as<std::string>());
  if (n["session_affinity"])
    s.session_affinity = parse_affinity_mode(n["session_affinity"].as<std::string>());
  if (n["read_write_split"])
    s.read_write_split = n["read_write_split"].as<bool>();
  return s;
}

RoutingConfig parse_routing(const YAML::Node& n) {
  RoutingConfig r;
  if (!n) return r;
  if (n["default_policy"])
    r.default_policy = parse_routing_policy(n["default_policy"].as<std::string>());
  if (n["read_write_split"])       r.read_write_split       = n["read_write_split"].as<bool>();
  if (n["send_writes_to_primary"]) r.send_writes_to_primary = n["send_writes_to_primary"].as<bool>();
  if (n["replication_aware"])      r.replication_aware      = n["replication_aware"].as<bool>();
  if (n["max_replica_lag_ms"])     r.max_replica_lag_ms     = n["max_replica_lag_ms"].as<uint32_t>();
  if (n["tenants"]) {
    const auto& t = n["tenants"];
    if (t["enabled"])       r.tenants.enabled       = t["enabled"].as<bool>();
    if (t["attribute_key"]) r.tenants.attribute_key = t["attribute_key"].as<std::string>();
    if (t["mappings"] && t["mappings"].IsSequence()) {
      for (const auto& m : t["mappings"]) {
        TenantMapping tm;
        if (m["tenant"])        tm.tenant       = m["tenant"].as<std::string>();
        if (m["backend_group"]) tm.backend_group = m["backend_group"].as<std::string>();
        r.tenants.mappings.push_back(std::move(tm));
      }
    }
  }
  return r;
}

SessionConfig parse_session(const YAML::Node& n) {
  SessionConfig s;
  if (!n) return s;
  if (n["recovery_mode"]) s.recovery_mode = parse_recovery_mode(n["recovery_mode"].as<std::string>());
  if (n["affinity"])      s.affinity      = parse_affinity_mode(n["affinity"].as<std::string>());
  if (n["max_idle_sec"])     s.max_idle_sec     = n["max_idle_sec"].as<uint32_t>();
  if (n["max_lifetime_sec"]) s.max_lifetime_sec = n["max_lifetime_sec"].as<uint32_t>();
  if (n["per_user"] && n["per_user"].IsSequence()) {
    for (const auto& u : n["per_user"]) {
      PerUserSessionConfig pu;
      if (u["user"]) pu.user = u["user"].as<std::string>();
      if (u["recovery_mode"])
        pu.recovery_mode = parse_recovery_mode(u["recovery_mode"].as<std::string>());
      if (u["affinity"])
        pu.affinity = parse_affinity_mode(u["affinity"].as<std::string>());
      s.per_user.push_back(std::move(pu));
    }
  }
  return s;
}

HealthCheckConfig parse_health_check(const YAML::Node& n) {
  HealthCheckConfig h;
  if (!n) return h;
  if (n["interval_ms"])        h.interval_ms        = n["interval_ms"].as<uint32_t>();
  if (n["timeout_ms"])         h.timeout_ms         = n["timeout_ms"].as<uint32_t>();
  if (n["failure_threshold"])  h.failure_threshold  = n["failure_threshold"].as<uint32_t>();
  if (n["recovery_threshold"]) h.recovery_threshold = n["recovery_threshold"].as<uint32_t>();
  if (n["method"])             h.method             = n["method"].as<std::string>();
  if (n["query"])              h.query              = n["query"].as<std::string>();
  return h;
}

FailoverConfig parse_failover(const YAML::Node& n) {
  FailoverConfig f;
  if (!n) return f;
  if (n["enabled"])            f.enabled            = n["enabled"].as<bool>();
  if (n["policy"])             f.policy             = parse_failover_policy(n["policy"].as<std::string>());
  if (n["grace_period_ms"])    f.grace_period_ms    = n["grace_period_ms"].as<uint32_t>();
  if (n["promotion_enabled"])  f.promotion_enabled  = n["promotion_enabled"].as<bool>();
  if (n["promotion_strategy"]) f.promotion_strategy = n["promotion_strategy"].as<std::string>();
  if (n["health_check"])       f.health_check = parse_health_check(n["health_check"]);
  if (n["query_recovery"]) {
    const auto& qr = n["query_recovery"];
    if (qr["enabled"])        f.query_recovery.enabled        = qr["enabled"].as<bool>();
    if (qr["retry_selects"])  f.query_recovery.retry_selects  = qr["retry_selects"].as<bool>();
    if (qr["retry_writes"])   f.query_recovery.retry_writes   = qr["retry_writes"].as<bool>();
    if (qr["max_retries"])    f.query_recovery.max_retries    = qr["max_retries"].as<uint32_t>();
    if (qr["retry_delay_ms"]) f.query_recovery.retry_delay_ms = qr["retry_delay_ms"].as<uint32_t>();
    if (qr["retry_backoff"])  f.query_recovery.retry_backoff  = qr["retry_backoff"].as<std::string>();
  }
  return f;
}

ClusterConfig parse_cluster(const YAML::Node& n) {
  ClusterConfig c;
  if (!n) return c;
  if (n["enabled"])   c.enabled   = n["enabled"].as<bool>();
  if (n["bind"])      c.bind      = n["bind"].as<std::string>();
  if (n["port"])      c.port      = static_cast<uint16_t>(n["port"].as<uint32_t>());
  if (n["discovery"]) c.discovery = n["discovery"].as<std::string>();
  if (n["seed_dns"])  c.seed_dns  = n["seed_dns"].as<std::string>();
  if (n["seed_nodes"] && n["seed_nodes"].IsSequence()) {
    for (const auto& sn : n["seed_nodes"])
      c.seed_nodes.push_back(sn.as<std::string>());
  }
  if (n["gossip"]) {
    const auto& g = n["gossip"];
    if (g["interval_ms"])     c.gossip.interval_ms     = g["interval_ms"].as<uint32_t>();
    if (g["fanout"])          c.gossip.fanout          = g["fanout"].as<uint32_t>();
    if (g["suspicion_mult"])  c.gossip.suspicion_mult  = g["suspicion_mult"].as<uint32_t>();
    if (g["retransmit_mult"]) c.gossip.retransmit_mult = g["retransmit_mult"].as<uint32_t>();
    if (g["sync_keys"] && g["sync_keys"].IsSequence()) {
      c.gossip.sync_keys.clear();
      for (const auto& k : g["sync_keys"])
        c.gossip.sync_keys.push_back(k.as<std::string>());
    }
  }
  return c;
}

FirewallConfig parse_firewall(const YAML::Node& n) {
  FirewallConfig f;
  if (!n) return f;
  if (n["enabled"])        f.enabled        = n["enabled"].as<bool>();
  if (n["default_action"]) f.default_action = n["default_action"].as<std::string>();
  if (n["rules"] && n["rules"].IsSequence()) {
    for (const auto& r : n["rules"]) {
      FirewallRule rule;
      if (r["name"])        rule.name        = r["name"].as<std::string>();
      if (r["match"])       rule.match       = r["match"].as<std::string>();
      if (r["match_regex"]) rule.match_regex = r["match_regex"].as<std::string>();
      if (r["action"])      rule.action      = r["action"].as<std::string>();
      if (r["message"])     rule.message     = r["message"].as<std::string>();
      if (r["schemas"] && r["schemas"].IsSequence())
        for (const auto& s : r["schemas"]) rule.schemas.push_back(s.as<std::string>());
      if (r["users"] && r["users"].IsSequence())
        for (const auto& u : r["users"]) rule.users.push_back(u.as<std::string>());
      f.rules.push_back(std::move(rule));
    }
  }
  return f;
}

RateLimitDimension parse_rate_limit_dim(const YAML::Node& n) {
  RateLimitDimension d;
  if (!n) return d;
  if (n["enabled"])          d.enabled          = n["enabled"].as<bool>();
  if (n["default_qps"])      d.default_qps      = n["default_qps"].as<uint32_t>();
  if (n["burst_multiplier"]) d.burst_multiplier = n["burst_multiplier"].as<double>();
  if (n["overrides"] && n["overrides"].IsSequence()) {
    for (const auto& o : n["overrides"]) {
      RateLimitOverride ov;
      // per_user overrides have "user:" key; per_schema have "schema:" key etc.
      if (o["user"])   ov.key = o["user"].as<std::string>();
      else if (o["schema"]) ov.key = o["schema"].as<std::string>();
      else if (o["tenant"]) ov.key = o["tenant"].as<std::string>();
      if (o["qps"]) ov.qps = o["qps"].as<uint32_t>();
      d.overrides.push_back(std::move(ov));
    }
  }
  return d;
}

LoggingConfig parse_logging(const YAML::Node& n) {
  LoggingConfig l;
  if (!n) return l;
  if (n["level"])  l.level  = n["level"].as<std::string>();
  if (n["format"]) l.format = n["format"].as<std::string>();
  if (n["output"]) l.output = n["output"].as<std::string>();
  if (n["file"]) {
    const auto& f = n["file"];
    if (f["path"])        l.file.path        = f["path"].as<std::string>();
    if (f["rotate"])      l.file.rotate      = f["rotate"].as<bool>();
    if (f["max_size_mb"]) l.file.max_size_mb = f["max_size_mb"].as<uint32_t>();
    if (f["max_backups"]) l.file.max_backups = f["max_backups"].as<uint32_t>();
    if (f["compress"])    l.file.compress    = f["compress"].as<bool>();
  }
  if (n["audit"]) {
    const auto& a = n["audit"];
    if (a["enabled"])            l.audit.enabled            = a["enabled"].as<bool>();
    if (a["output"])             l.audit.output             = a["output"].as<std::string>();
    if (a["file"])               l.audit.file               = a["file"].as<std::string>();
    if (a["log_queries"])        l.audit.log_queries        = a["log_queries"].as<bool>();
    if (a["log_connections"])    l.audit.log_connections    = a["log_connections"].as<bool>();
    if (a["log_failovers"])      l.audit.log_failovers      = a["log_failovers"].as<bool>();
    if (a["log_auth"])           l.audit.log_auth           = a["log_auth"].as<bool>();
    if (a["log_config_changes"]) l.audit.log_config_changes = a["log_config_changes"].as<bool>();
    if (a["log_firewall_hits"])  l.audit.log_firewall_hits  = a["log_firewall_hits"].as<bool>();
  }
  return l;
}

Config parse_config(const YAML::Node& root) {
  Config cfg;

  // node
  if (const auto& n = root["node"]) {
    if (n["id"])       cfg.node.id       = n["id"].as<std::string>();
    if (n["name"])     cfg.node.name     = n["name"].as<std::string>();
    if (n["data_dir"]) cfg.node.data_dir = n["data_dir"].as<std::string>();
    if (n["mode"])     cfg.node.mode     = parse_node_mode(n["mode"].as<std::string>());
  }

  // listeners
  if (const auto& l = root["listeners"]) {
    cfg.listeners.mysql    = parse_listener(l["mysql"],   3306);
    cfg.listeners.postgres = parse_listener(l["postgres"], 5432);
    cfg.listeners.admin    = parse_listener(l["admin"],    8080);
    cfg.listeners.admin.enabled = true;  // admin always on
  } else {
    cfg.listeners.mysql.port    = 3306;
    cfg.listeners.postgres.port = 5432;
    cfg.listeners.admin.port    = 8080;
    cfg.listeners.admin.enabled = true;
  }

  // tls
  if (const auto& t = root["tls"]) {
    static_cast<TlsEndpointConfig&>(cfg.tls.frontend) = parse_tls_endpoint(t["frontend"]);
    static_cast<TlsEndpointConfig&>(cfg.tls.backend)  = parse_tls_endpoint(t["backend"]);
    if (t["backend"] && t["backend"]["verify_server"])
      cfg.tls.backend.verify_server = t["backend"]["verify_server"].as<bool>();
  }

  // backends
  if (const auto& bs = root["backends"]; bs && bs.IsSequence())
    for (const auto& b : bs)
      cfg.backends.push_back(parse_backend(b));

  // schemas
  if (const auto& ss = root["schemas"]; ss && ss.IsSequence())
    for (const auto& s : ss)
      cfg.schemas.push_back(parse_schema(s));

  cfg.routing  = parse_routing(root["routing"]);
  cfg.session  = parse_session(root["session"]);
  cfg.failover = parse_failover(root["failover"]);
  cfg.cluster  = parse_cluster(root["cluster"]);

  // auth
  if (const auto& a = root["auth"]) {
    if (a["method"]) cfg.auth.method = a["method"].as<std::string>();
    if (a["local"] && a["local"]["users_file"])
      cfg.auth.local.users_file = a["local"]["users_file"].as<std::string>();
    if (a["ldap"]) {
      const auto& ld = a["ldap"];
      if (ld["enabled"])          cfg.auth.ldap.enabled          = ld["enabled"].as<bool>();
      if (ld["url"])              cfg.auth.ldap.url              = ld["url"].as<std::string>();
      if (ld["base_dn"])          cfg.auth.ldap.base_dn          = ld["base_dn"].as<std::string>();
      if (ld["bind_dn"])          cfg.auth.ldap.bind_dn          = ld["bind_dn"].as<std::string>();
      if (ld["bind_password"])    cfg.auth.ldap.bind_password    = ld["bind_password"].as<std::string>();
      if (ld["user_filter"])      cfg.auth.ldap.user_filter      = ld["user_filter"].as<std::string>();
      if (ld["group_attribute"])  cfg.auth.ldap.group_attribute  = ld["group_attribute"].as<std::string>();
      if (ld["timeout_ms"])       cfg.auth.ldap.timeout_ms       = ld["timeout_ms"].as<uint32_t>();
      if (ld["start_tls"])        cfg.auth.ldap.start_tls        = ld["start_tls"].as<bool>();
    }
    if (a["jwt"]) {
      const auto& jw = a["jwt"];
      if (jw["enabled"])    cfg.auth.jwt.enabled    = jw["enabled"].as<bool>();
      if (jw["algorithm"])  cfg.auth.jwt.algorithm  = jw["algorithm"].as<std::string>();
      if (jw["public_key"]) cfg.auth.jwt.public_key = jw["public_key"].as<std::string>();
      if (jw["secret"])     cfg.auth.jwt.secret     = jw["secret"].as<std::string>();
      if (jw["issuer"])     cfg.auth.jwt.issuer     = jw["issuer"].as<std::string>();
      if (jw["audience"])   cfg.auth.jwt.audience   = jw["audience"].as<std::string>();
      if (jw["leeway_sec"]) cfg.auth.jwt.leeway_sec = jw["leeway_sec"].as<uint32_t>();
    }
  }

  // rbac
  if (const auto& rb = root["rbac"]) {
    if (rb["enabled"])    cfg.rbac.enabled    = rb["enabled"].as<bool>();
    if (rb["roles_file"]) cfg.rbac.roles_file = rb["roles_file"].as<std::string>();
  }

  cfg.firewall   = parse_firewall(root["firewall"]);

  // rate_limit
  if (const auto& rl = root["rate_limit"]) {
    if (rl["enabled"]) cfg.rate_limit.enabled = rl["enabled"].as<bool>();
    cfg.rate_limit.per_user   = parse_rate_limit_dim(rl["per_user"]);
    cfg.rate_limit.per_ip     = parse_rate_limit_dim(rl["per_ip"]);
    cfg.rate_limit.per_schema = parse_rate_limit_dim(rl["per_schema"]);
    cfg.rate_limit.per_tenant = parse_rate_limit_dim(rl["per_tenant"]);
  }

  // api
  if (const auto& ap = root["api"]) {
    if (ap["enabled"])    cfg.api.enabled    = ap["enabled"].as<bool>();
    if (ap["bind"])       cfg.api.bind       = ap["bind"].as<std::string>();
    if (ap["port"])       cfg.api.port       = static_cast<uint16_t>(ap["port"].as<uint32_t>());
    if (ap["auth"])       cfg.api.auth       = ap["auth"].as<bool>();
    if (ap["token_file"]) cfg.api.token_file = ap["token_file"].as<std::string>();
    if (ap["ui"]) {
      if (ap["ui"]["enabled"]) cfg.api.ui.enabled = ap["ui"]["enabled"].as<bool>();
      if (ap["ui"]["path"])    cfg.api.ui.path    = ap["ui"]["path"].as<std::string>();
    }
    if (ap["cors"]) {
      if (ap["cors"]["enabled"]) cfg.api.cors.enabled = ap["cors"]["enabled"].as<bool>();
      if (ap["cors"]["origins"] && ap["cors"]["origins"].IsSequence())
        for (const auto& o : ap["cors"]["origins"])
          cfg.api.cors.origins.push_back(o.as<std::string>());
    }
    if (ap["max_request_body_kb"]) cfg.api.max_request_body_kb = ap["max_request_body_kb"].as<uint32_t>();
    if (ap["request_timeout_ms"])  cfg.api.request_timeout_ms  = ap["request_timeout_ms"].as<uint32_t>();
  }

  // metrics
  if (const auto& mt = root["metrics"]) {
    if (mt["prometheus"]) {
      const auto& pr = mt["prometheus"];
      if (pr["enabled"]) cfg.metrics.prometheus.enabled = pr["enabled"].as<bool>();
      if (pr["bind"])    cfg.metrics.prometheus.bind    = pr["bind"].as<std::string>();
      if (pr["port"])    cfg.metrics.prometheus.port    = static_cast<uint16_t>(pr["port"].as<uint32_t>());
      if (pr["path"])    cfg.metrics.prometheus.path    = pr["path"].as<std::string>();
      if (pr["auth"])    cfg.metrics.prometheus.auth    = pr["auth"].as<bool>();
    }
    if (mt["tracing"]) {
      const auto& tr = mt["tracing"];
      if (tr["enabled"])         cfg.metrics.tracing.enabled         = tr["enabled"].as<bool>();
      if (tr["exporter"])        cfg.metrics.tracing.exporter        = tr["exporter"].as<std::string>();
      if (tr["endpoint"])        cfg.metrics.tracing.endpoint        = tr["endpoint"].as<std::string>();
      if (tr["service_name"])    cfg.metrics.tracing.service_name    = tr["service_name"].as<std::string>();
      if (tr["service_version"]) cfg.metrics.tracing.service_version = tr["service_version"].as<std::string>();
      if (tr["sample_rate"])     cfg.metrics.tracing.sample_rate     = tr["sample_rate"].as<double>();
      if (tr["propagation"])     cfg.metrics.tracing.propagation     = tr["propagation"].as<std::string>();
    }
  }

  cfg.logging = parse_logging(root["logging"]);

  return cfg;
}

} // namespace

// ── ConfigLoader ──────────────────────────────────────────────────────────

Result<Config, std::string> ConfigLoader::load(const std::string& path) {
  try {
    if (!std::filesystem::exists(path))
      return Err(std::string("config file not found: ") + path);

    YAML::Node root = YAML::LoadFile(path);
    Config cfg      = parse_config(root);

    if (auto err = validate(cfg); err.has_value())
      return Err(std::move(*err));

    return Ok(std::move(cfg));

  } catch (const YAML::Exception& e) {
    return Err(std::string("YAML parse error: ") + e.what());
  } catch (const std::exception& e) {
    return Err(std::string("config error: ") + e.what());
  }
}

std::optional<std::string> ConfigLoader::validate(const Config& cfg) {
  // Required fields
  if (cfg.node.id.empty())
    return "node.id is required";

  // Port ranges
  auto check_port = [](uint16_t p, const char* name) -> std::optional<std::string> {
    if (p == 0) return std::string(name) + " port cannot be 0";
    return std::nullopt;
  };

  if (cfg.listeners.mysql.enabled)
    if (auto e = check_port(cfg.listeners.mysql.port, "listeners.mysql")) return e;
  if (cfg.listeners.postgres.enabled)
    if (auto e = check_port(cfg.listeners.postgres.port, "listeners.postgres")) return e;
  if (auto e = check_port(cfg.api.port, "api")) return e;

  // Backend IDs must be unique and have required fields
  std::set<std::string> backend_ids;
  for (const auto& b : cfg.backends) {
    if (b.id.empty())   return "backend is missing 'id'";
    if (b.host.empty()) return "backend '" + b.id + "' is missing 'host'";
    if (b.port == 0)    return "backend '" + b.id + "' port cannot be 0";
    if (b.group.empty()) return "backend '" + b.id + "' is missing 'group'";
    if (!backend_ids.insert(b.id).second)
      return "duplicate backend id '" + b.id + "'";
  }

  // Schema names must be unique and have required fields
  std::set<std::string> schema_names;
  for (const auto& s : cfg.schemas) {
    if (s.name.empty())          return "schema is missing 'name'";
    if (s.backend_group.empty()) return "schema '" + s.name + "' is missing 'backend_group'";
    if (!schema_names.insert(s.name).second)
      return "duplicate schema name '" + s.name + "'";
  }

  // At least one backend and one schema if MySQL listener is enabled
  if (cfg.listeners.mysql.enabled && cfg.backends.empty())
    return "listeners.mysql is enabled but no backends are configured";

  if (cfg.listeners.mysql.enabled && cfg.schemas.empty())
    return "listeners.mysql is enabled but no schemas are configured";

  return std::nullopt;
}

// ── ConfigReloader ────────────────────────────────────────────────────────

ConfigReloader::ConfigReloader(std::string path, Callback on_reload)
    : path_(std::move(path)), on_reload_(std::move(on_reload)) {}

void ConfigReloader::reload() {
  auto result = ConfigLoader::load(path_);
  if (is_err(result)) {
    // Keep running with existing config — log is written by Application
    return;
  }
  on_reload_(std::make_shared<const Config>(std::move(get_value(result))));
}

} // namespace dbmesh
