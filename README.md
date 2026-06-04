# DBMesh

<p align="center">
  <strong>Distributed Database Routing Mesh for MySQL, MariaDB, PostgreSQL, and Cloud Databases</strong>
</p>

<p align="center">
  Schema-aware routing · Failover orchestration · Session recovery · Distributed clustering · Connection pooling
</p>

---

DBMesh is a high-performance, distributed database routing mesh written in C++20. It provides a single endpoint for applications while transparently routing traffic to the correct backend database based on schemas, tenants, routing policies, and cluster state — without requiring any application changes.

```text
                        Applications
                             |
                             v
                      +-----------+
                      |  DBMesh   |
                      +-----------+
                             |
              +--------------+--------------+
              |              |              |
              v              v              v
           MySQL         MariaDB       PostgreSQL
        (primary +      (primary +    (primary +
         replicas)       replicas)     replicas)
```

---

## Project status

> ⚠️ **Early active development.** DBMesh is being built milestone by milestone
> (Phase 1: MySQL + MariaDB). This README describes the full intended product;
> not all of it is implemented yet. See [`plan.md`](plan.md) for the live
> milestone checklist.
>
> **Working today:** process lifecycle + config + structured logging
> (Milestone 1.1) and the MySQL wire-protocol frontend — a real `mysql` client
> can connect, authenticate, and query (Milestone 1.2). Query routing,
> connection pooling, failover, clustering, and the admin API/UI are upcoming.
>
> **Building from source** (dev toolchain — Clang 14 / Boost 1.74 on Ubuntu
> 22.04; see [`CLAUDE.md`](CLAUDE.md) for the full constraint list):
>
> ```bash
> cmake -GNinja -DCMAKE_BUILD_TYPE=Debug \
>   -DCMAKE_C_COMPILER=clang-14 -DCMAKE_CXX_COMPILER=clang++-14 -B build
> ninja -C build && ctest --test-dir build --output-on-failure
> ```

---

## Why DBMesh?

Modern SaaS and enterprise platforms manage hundreds or thousands of databases across MySQL, MariaDB, PostgreSQL, AWS RDS, multi-region deployments, and database-per-tenant architectures.

Without a routing layer, applications must manage:

- Database discovery and endpoint tracking
- Tenant-to-shard mappings
- Failover detection and reconnection logic
- Replica selection and lag awareness
- Connection pooling per backend
- Session state during failover

DBMesh abstracts all of this. Applications connect to one endpoint. DBMesh handles the rest.

---

## Key Differentiators

| | DBMesh | ProxySQL | PgBouncer | RDS Proxy | Vitess |
|---|---|---|---|---|---|
| Multi-engine | ✓ | MySQL only | PG only | AWS only | MySQL only |
| Schema-aware routing | ✓ | Limited | ✗ | ✗ | ✓ |
| Gossip clustering | ✓ | ✗ | ✗ | ✗ | ✓ |
| Session recovery modes | ✓ | ✗ | ✗ | ✗ | ✗ |
| Self-hosted | ✓ | ✓ | ✓ | ✗ | ✓ |
| No schema modifications | ✓ | ✓ | ✓ | ✓ | ✗ |
| Distributed control plane | ✓ | ✗ | ✗ | ✗ | ✓ |

---

## Features

### Schema-Aware Routing

Route queries automatically based on schema names extracted directly from SQL.

```sql
SELECT * FROM billing.users;
```

DBMesh resolves:

```text
billing → mysql-prod-group
```

No application changes required.

---

### Session-Aware Routing

DBMesh tracks session state and routes subsequent queries automatically after a `USE` statement.

```sql
USE billing;
SELECT * FROM users;   -- automatically routed to billing backend group
```

---

### Read / Write Splitting

Transparently route reads to replicas and writes to the primary.

```text
SELECT          → replica pool  (round-robin across healthy replicas)
INSERT          → primary
UPDATE          → primary
DELETE          → primary
BEGIN / COMMIT  → primary (session pinned for transaction duration)
```

Replication-aware: replicas exceeding the configured lag threshold are automatically excluded from the read pool.

---

### Multi-Database Support

**MySQL**
- 5.4, 5.7, 8.x

**MariaDB**
- 10.x, 11.x

**PostgreSQL** *(Phase 2)*
- 9, 10, 11, 12, 13, 14, 15, 16, 17

**Cloud** *(Phase 3)*
- AWS RDS MySQL
- AWS RDS MariaDB
- AWS RDS PostgreSQL
- Amazon Aurora (writer/reader endpoint aware)

---

### Routing Policies

| Policy | Description |
|---|---|
| `round_robin` | Distribute evenly across backends |
| `weighted` | Distribute proportionally by backend weight |
| `least_connections` | Route to backend with fewest active connections |
| `latency` | Route to backend with lowest recent response time |
| `random` | Random backend selection |
| `failover` | Primary with ordered fallback list |
| `consistent_hashing` | Planned — deterministic key-based routing |

Policies are configurable globally, per schema, and per tenant.

---

### Multi-Tenant Routing

Map tenants to specific database clusters. Applications connect to DBMesh using a tenant attribute; DBMesh resolves the correct backend group.

```text
tenant: acme    →  mysql-cluster-us-east
tenant: globex  →  mysql-cluster-eu-west
tenant: initech →  rds-mysql-prod
```

---

### Connection Pooling

Per-backend connection pools with full lifecycle management.

- Primary and replica pools managed independently
- Pool warming: pre-connect minimum connections on startup
- Configurable min/max pool size per backend
- Idle connection cleanup on configurable TTL
- Health-aware: unhealthy backends removed from pool automatically
- Connect timeout and retry configuration
- 100,000+ concurrent client sessions supported

---

### Session Recovery Framework

Pluggable session recovery controls what state is tracked per session and replayed on failover. Configurable globally, per schema, and per user.

#### STATELESS

No session tracking. Maximum throughput. Lowest memory.

Best for: analytics workloads, reporting, read-only services.

#### BASIC

Tracks and replays on failover:

```sql
USE <database>
SET NAMES utf8mb4
SET autocommit = 1
SET sql_mode = '...'
SET time_zone = '...'
```

Best for: most application workloads.

#### ADVANCED

Tracks everything in BASIC plus:

```sql
SET @userid = 123          -- user-defined variables
PREPARE stmt FROM '...'    -- prepared statements
DEALLOCATE PREPARE stmt
```

Best for: applications using prepared statements and session variables.

#### STRICT

Tracks all recoverable session state:

- Everything in ADVANCED
- Open cursors
- Advisory locks
- Temp table existence (detection only — cannot recreate)
- Full transaction state

If a session cannot be recovered (e.g. a temp table existed on the failed backend), DBMesh returns `SESSION_RECOVERY_FAILED` to the client rather than silently continuing on a broken session.

Best for: legacy applications, payment services, strict transactional workloads.

#### Per-schema and per-user overrides

```yaml
session:
  recovery_mode: BASIC        # global default

schemas:
  - name: billing
    session_recovery: STRICT  # billing needs full recovery

  - name: analytics
    session_recovery: STATELESS  # analytics can lose session

session:
  per_user:
    - user: payment_service
      recovery_mode: STRICT
    - user: reporting
      recovery_mode: STATELESS
```

#### Tracker plugin framework

Recovery modes are implemented as composable tracker plugins, not hardcoded if/else logic. Active trackers are determined by the configured recovery mode.

| Plugin | Tracks |
|---|---|
| `DatabaseTracker` | Active schema / USE statements |
| `VariableTracker` | SET session variables |
| `PreparedStatementTracker` | PREPARE / DEALLOCATE lifecycle |
| `TransactionTracker` | BEGIN / COMMIT / ROLLBACK state |
| `TempTableTracker` | CREATE TEMPORARY TABLE detection |
| `CursorTracker` | Open cursor state |
| `LockTracker` | Advisory lock state |

Trackers can be individually enabled in configuration for custom recovery profiles.

#### Capability matrix

The management API exposes exactly what will and will not survive failover for any live session:

```http
GET /api/v1/sessions/1234/capabilities
```

```json
{
  "session_id": 1234,
  "recoverable": true,
  "recovery_mode": "ADVANCED",
  "tracking": {
    "database": true,
    "variables": true,
    "prepared_statements": true,
    "temp_tables": false,
    "transactions": false,
    "cursors": false,
    "locks": false
  }
}
```

---

### Session Affinity

Controls when DBMesh pins a session to a specific backend.

| Mode | Behaviour |
|---|---|
| `NONE` | Free routing — each query routed independently |
| `SCHEMA` | Session pinned after first `USE` statement |
| `BACKEND` | Session pinned to one backend for its entire lifetime |
| `TRANSACTION` | Pinned during `BEGIN..COMMIT`, released after |

`TRANSACTION` is the recommended default for most workloads — it prevents cross-backend splits mid-transaction while allowing free routing for non-transactional queries.

---

### Failover Engine

#### Failover policies

| Policy | Behaviour |
|---|---|
| `IMMEDIATE` | Switch to standby the moment failure is detected |
| `GRACEFUL` | Wait for active queries to complete (configurable drain window), then switch |
| `MANUAL` | Mark backend failed, require operator action via API |

#### Health check methods

| Method | Checks |
|---|---|
| `tcp` | TCP connect to backend port |
| `login` | Full authentication handshake |
| `query` | Execute `SELECT 1` and verify response |
| `replication` | Check replication status and lag |

Configurable failure threshold (consecutive failures before marking failed) and recovery threshold (consecutive successes to restore).

#### Query retry policy

```yaml
failover:
  query_recovery:
    retry_selects: true   # safe — idempotent reads can be retried
    retry_writes: false   # never auto-retry writes — risk of duplicate execution
    max_retries: 3
    retry_delay_ms: 100
```

#### Session migration

On failover, DBMesh migrates sessions to the new backend and replays tracked state according to the session recovery mode. Sessions in STATELESS mode reconnect without replay. Sessions in STRICT mode that cannot be fully recovered receive `SESSION_RECOVERY_FAILED`.

---

### Distributed Clustering

#### Active-active architecture

Deploy multiple DBMesh nodes. All nodes are active and can accept client connections. There is no single point of failure.

```text
DBMesh-A  ←→  DBMesh-B  ←→  DBMesh-C
    ↕               ↕               ↕
   DB              DB              DB
```

#### Gossip protocol

DBMesh uses a SWIM-inspired gossip protocol to synchronize cluster state across nodes in under 1 second.

Synchronized state:

- Schema registry
- Backend health state
- Cluster membership
- Config versions
- Routing table changes

#### Peer discovery

**Static:**
```yaml
cluster:
  seed_nodes:
    - 10.0.0.1:7946
    - 10.0.0.2:7946
```

**DNS:**
```yaml
cluster:
  seed_dns: dbmesh.internal
```

**Hybrid:** both methods active simultaneously.

---

### Security

#### TLS

- **Frontend TLS** — encrypt all client-to-DBMesh connections
- **Backend TLS** — encrypt all DBMesh-to-database connections
- **Mutual TLS (mTLS)** — require and verify client certificates
- Configurable minimum TLS version

#### Authentication

| Method | Description |
|---|---|
| `local` | Username/password, bcrypt-hashed, stored in `users.yaml` |
| `ldap` | Bind against LDAP/Active Directory |
| `jwt` | Validate signed JWT tokens (RS256/HS256) |
| `oidc` | OpenID Connect *(planned)* |

#### RBAC

| Role | Permissions |
|---|---|
| `admin` | Full access — all API endpoints, all configuration |
| `operator` | Manage backends, trigger failover, view sessions |
| `auditor` | Read-only access to audit logs and metrics |
| `viewer` | Read-only access to dashboards and session state |

---

### Query Firewall

Protect databases from dangerous operations with rule-based query interception.

```yaml
firewall:
  enabled: true
  rules:
    - name: no-drop-database
      match: DROP DATABASE
      action: block

    - name: no-truncate
      match: TRUNCATE
      action: block

    - name: warn-unfiltered-delete
      match_regex: 'DELETE FROM \w+ WHERE\s*$'
      action: warn   # log and allow
```

Actions: `block` (return error to client), `warn` (log and allow), `log` (record only).

---

### Rate Limiting

Protect backends from overload with configurable rate limits.

```yaml
rate_limit:
  per_user:
    default_qps: 5000
    overrides:
      - user: reporting
        qps: 500
  per_schema:
    overrides:
      - schema: analytics
        qps: 2000
  per_ip:
    default_qps: 10000
  per_tenant:
    default_qps: 5000
```

---

### Management API

RESTful HTTP API served on a dedicated admin port.

| Endpoint | Method | Description |
|---|---|---|
| `/api/v1/schemas` | GET, POST, PUT, DELETE | Manage schema registry |
| `/api/v1/backends` | GET, POST, PUT, DELETE | Manage backend definitions |
| `/api/v1/nodes` | GET | List cluster nodes and health |
| `/api/v1/sessions` | GET | List active sessions |
| `/api/v1/sessions/:id/capabilities` | GET | Session recovery capability matrix |
| `/api/v1/metrics` | GET | Current metrics snapshot |
| `/api/v1/failover` | POST | Trigger manual failover |
| `/api/v1/audit-logs` | GET | Query audit log entries |
| `/api/v1/routing/analyze` | POST | Trace routing path for a query |
| `/api/v1/config/reload` | POST | Hot reload configuration |

All endpoints require authentication. Token configured in `dbmesh.yaml`.

---

### Management UI

Built-in React + TypeScript web interface, served from the admin port at `/ui`.

#### Dashboard
- Live QPS, active connections, backend latency
- Failover event count and recent history
- Cluster health summary

#### Cluster view
- Node list with health status and gossip state
- Per-node connection counts and load

#### Schema manager
- Add, edit, and delete schema-to-backend-group mappings
- Per-schema routing policy and recovery mode overrides

#### Backend manager
- Add and remove backends
- Real-time health status, latency, and pool utilization
- Trigger manual health check

#### Session viewer
- Live session list with user, schema, backend, and affinity state
- Per-session capability matrix (what survives failover)
- Kill session

#### Routing analyzer
- Trace any SQL query through the routing engine
- Shows schema extraction, policy applied, backend selected

#### Metrics view
- Prometheus-backed graphs for all DBMesh metrics
- Configurable time window

#### Audit logs
- Searchable log of all configuration changes, auth events, and failover events

---

### Observability

#### Prometheus metrics

```text
dbmesh_connections_total          # total client connections since start
dbmesh_connections_active         # current active client sessions
dbmesh_qps                        # queries per second (labelled by schema, backend)
dbmesh_query_duration_ms          # query latency histogram
dbmesh_failovers_total            # total failover events (labelled by backend)
dbmesh_session_recovery_total     # session recovery attempts (labelled by mode, result)
dbmesh_backend_latency_ms         # backend response latency histogram
dbmesh_pool_size                  # connection pool size per backend
dbmesh_pool_wait_ms               # time spent waiting for pool connection
dbmesh_replication_lag_ms         # replication lag per replica backend
dbmesh_gossip_rounds_total        # gossip protocol rounds
dbmesh_config_propagation_ms      # config change propagation latency
```

Metrics endpoint: `http://<host>:9090/metrics`

#### OpenTelemetry tracing

Per-query spans with configurable sampling rate. Supports OTLP, Jaeger, and Zipkin exporters.

#### Structured logging

JSON and plain-text formats. Log levels: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`.

Audit logging: all management API calls, authentication events, failover events, and configuration changes are written to a separate audit log and stored in SQLite.

---

### Performance Goals

| Metric | Target |
|---|---|
| Concurrent connections | 100,000+ |
| Routing lookup | O(1) |
| Routing overhead | < 100 microseconds |
| Failover detection | 100ms (configurable) |
| Config propagation | < 1 second across cluster |
| Availability | 99.99% |

---

## Architecture

```text
                           Clients
                               |
              +----------------+----------------+
              |                                 |
        MySQL frontend                  PostgreSQL frontend
         (Phase 1)                          (Phase 2)
              |                                 |
              +-----------------+---------------+
                                |
                          Routing core
                                |
              +-----------------+-----------------+
              |                                   |
        Control plane                        Data plane
              |                                   |
   +----------+----------+            +-----------+-----------+
   |          |          |            |           |           |
Cluster   Schema     Security     Session     Failover    Connection
manager  registry    layer        manager     engine      pool mgr
   |          |                       |           |           |
   +----------+           +-----------+-----------+           |
              |           |                                   |
           Gossip    Health monitor                    Backend pools
           protocol
```

### Internal components

**Frontend protocol layer** — handles authentication, SSL negotiation, session creation, and query parsing. Separate implementations for MySQL/MariaDB and PostgreSQL wire protocols.

**Query router** — extracts schema from SQL, resolves tenant, selects backend, enforces routing policy and firewall rules. O(1) lookup via hash-indexed schema registry.

**Session manager** — stores per-session state: ID, user, schema, backend, session variables, prepared statements, transaction state. Drives tracker plugin framework.

**Connection pool manager** — per-backend pools with primary and replica pools managed independently. Pool warming, idle cleanup, health-aware connection allocation.

**Health monitor** — configurable check methods (TCP, login, query, replication). Maintains backend state machine: `HEALTHY → DEGRADED → FAILED → RECOVERING → HEALTHY`.

**Failover engine** — detects failure via health monitor, executes failover policy (IMMEDIATE/GRACEFUL/MANUAL), promotes replicas, migrates sessions, replays recovery state.

**Cluster manager** — manages node membership via SWIM-inspired gossip. Handles peer discovery (static, DNS, hybrid). Synchronizes schema registry, backend state, health, and config versions across all nodes in under 1 second.

**Persistence layer** — SQLite storage for schemas, backends, nodes, users, roles, audit logs, and routing rules. Gossip-synchronized across the cluster.

---

## Configuration

See `dbmesh.yaml` for the full annotated configuration reference.

Minimal working configuration for a single MySQL primary + replica:

```yaml
node:
  id: node-01
  mode: standalone

listeners:
  mysql:
    enabled: true
    port: 3306

backends:
  - id: mysql-primary
    type: mysql
    host: 10.0.1.10
    port: 3306
    role: primary
    group: prod
    user: dbmesh
    password: secret

  - id: mysql-replica
    type: mysql
    host: 10.0.1.11
    port: 3306
    role: replica
    group: prod
    user: dbmesh
    password: secret

schemas:
  - name: default
    backend_group: prod

routing:
  read_write_split: true

session:
  recovery_mode: BASIC
```

---

## Deployment

### Bare metal / VM

```bash
./dbmesh --config /etc/dbmesh/dbmesh.yaml
```

### Docker

```bash
docker run -d \
  -v /etc/dbmesh:/etc/dbmesh \
  -p 3306:3306 \
  -p 8080:8080 \
  ghcr.io/dbmesh/dbmesh:latest \
  --config /etc/dbmesh/dbmesh.yaml
```

### Kubernetes

```bash
helm install dbmesh ./charts/dbmesh \
  --set config.mode=cluster \
  --set replicaCount=3
```

### Hot config reload

Configuration changes can be applied without restarting:

```bash
curl -X POST http://localhost:8080/api/v1/config/reload
```

---

## Technology Stack

| Component | Technology |
|---|---|
| Core language | C++20 |
| Networking | Boost.Asio |
| HTTP API | Boost.Beast |
| JSON | Boost.JSON |
| UUID | Boost.UUID |
| Persistence | SQLite |
| Build system | CMake |
| Frontend | React + TypeScript |
| Metrics | Prometheus |
| Tracing | OpenTelemetry |

---

## Repository Structure

```text
dbmesh/
├── core/                   # core types, config, lifecycle
├── protocol/
│   ├── mysql/              # MySQL/MariaDB wire protocol
│   └── postgres/           # PostgreSQL wire protocol (Phase 2)
├── routing/                # query router, schema registry, policies
├── session/                # session manager, tracker plugins
├── failover/               # failover engine, health monitor
├── cluster/                # cluster manager, gossip protocol
├── pool/                   # connection pool manager
├── security/               # auth, TLS, RBAC, firewall, rate limiting
├── storage/                # SQLite persistence layer
├── api/                    # REST management API (Boost.Beast)
├── metrics/                # Prometheus + OpenTelemetry
├── ui/                     # React + TypeScript admin UI
├── tests/                  # unit and integration tests
├── docs/                   # documentation
├── charts/                 # Helm chart
├── docker/                 # Dockerfile and compose files
└── examples/               # example configurations
```

---

## Roadmap

### Phase 1 — MySQL + MariaDB *(current)*

Full feature set against MySQL and MariaDB: read/write splitting, schema routing, tenant routing, connection pooling, session recovery framework, failover engine, gossip clustering, security (TLS, RBAC, LDAP, JWT), query firewall, rate limiting, management API, admin UI, Prometheus metrics, OpenTelemetry tracing.

### Phase 2 — PostgreSQL

PostgreSQL wire protocol frontend plugs into the existing infrastructure. New additions: PG extended query protocol (Parse/Bind/Execute), `search_path` tracker, portal tracker, replication slot monitoring, `pg_promote()` support.

### Phase 3 — Cloud databases

AWS RDS adapters for MySQL, MariaDB, and PostgreSQL. RDS endpoint validation, Multi-AZ failover awareness, Amazon Aurora writer/reader endpoint mapping.

### Future

- Cross-database federation (`JOIN` across MySQL and PostgreSQL backends)
- AI routing advisor (analyze load, latency, query patterns, recommend routing changes)
- Auto schema rebalancing (move schemas between backend groups automatically)
- Live migration (on-prem → RDS, zero application changes)
- Consistent hashing routing policy
- OIDC authentication

---

## Use Cases

**SaaS platforms** — database-per-tenant architecture with a single application endpoint. Add tenants and reassign schemas without application changes.

**Managed database providers** — centralized routing, failover, and observability across thousands of customer databases.

**Enterprise applications** — unify routing across multi-database environments without modifying application connection strings.

**Cloud migrations** — route traffic to on-premise or cloud backends interchangeably. Migrate databases behind DBMesh with zero application changes.

**Hybrid deployments** — mix on-premise and cloud databases in the same routing mesh.

---

## Vision

> DBMesh aims to become the service mesh for relational databases.
>
> One endpoint. One routing layer. One control plane. Any database.
