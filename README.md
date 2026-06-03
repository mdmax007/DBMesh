# DBMesh

**Distributed Database Routing Mesh for MySQL, MariaDB, PostgreSQL, and Cloud Databases**

DBMesh is a high-performance, distributed database routing mesh that provides schema-aware routing, tenant-aware routing, failover orchestration, session recovery, connection pooling, service discovery, and clustering for relational databases.

Instead of applications managing database locations, failovers, replicas, and tenant mappings, DBMesh acts as a unified database access layer that intelligently routes traffic to the correct backend database.

---

## Why DBMesh?

Modern SaaS and enterprise platforms often manage hundreds or thousands of databases across:

* MySQL
* MariaDB
* PostgreSQL
* AWS RDS
* Multi-region deployments
* Database-per-tenant architectures

Applications become tightly coupled to infrastructure and must manage:

* Database discovery
* Tenant mappings
* Failover logic
* Replica selection
* Shard placement
* Connection pooling

DBMesh abstracts these concerns and presents a single endpoint to applications.

```text
                 Applications
                        |
                        v

                  +-----------+
                  |  DBMesh   |
                  +-----------+

          +-----------+-----------+
          |           |           |
          v           v           v

       MySQL      PostgreSQL    MariaDB
```

---

## Key Features

### Schema-Aware Routing

Route queries automatically based on schema names.

```sql
SELECT * FROM billing.users;
```

DBMesh resolves:

```text
billing -> mysql-prod-01
```

without requiring application changes.

---

### Session-Aware Routing

Supports:

```sql
USE billing;

SELECT * FROM users;
```

DBMesh tracks session state and routes subsequent queries automatically.

---

### Multi-Database Support

Supported backends:

* MySQL 5.4+
* MySQL 5.7+
* MySQL 8.x
* MariaDB 10+
* MariaDB 11+
* PostgreSQL 9+
* PostgreSQL 10+
* PostgreSQL 11+
* PostgreSQL 12+
* PostgreSQL 13+
* PostgreSQL 14+
* PostgreSQL 15+
* PostgreSQL 16+
* PostgreSQL 17+
* AWS RDS MySQL
* AWS RDS MariaDB
* AWS RDS PostgreSQL

---

### Distributed Clustering

Deploy multiple DBMesh nodes.

```text
DBMesh-A
DBMesh-B
DBMesh-C
```

Features:

* Active-active architecture
* Gossip-based synchronization
* Peer discovery
* Distributed configuration
* High availability

---

### Automatic Failover

Supports:

* Primary/Replica failover
* Backend health monitoring
* Session recovery
* Query retry policies

Failover modes:

* Immediate
* Graceful
* Manual

---

### Session Recovery

Choose recovery behavior globally or per schema.

Modes:

#### Stateless

Maximum performance.

No session tracking.

#### Basic

Tracks:

* USE database
* SET NAMES
* AUTOCOMMIT
* SQL_MODE
* TIMEZONE

#### Advanced

Tracks:

* Session variables
* Prepared statements

#### Strict

Tracks all recoverable session state.

---

### Routing Policies

Supported routing strategies:

* Round Robin
* Weighted Round Robin
* Least Connections
* Latency Based
* Random
* Failover
* Consistent Hashing (planned)

---

### Read/Write Splitting

Automatically route:

```text
SELECT  -> Replica
INSERT  -> Primary
UPDATE  -> Primary
DELETE  -> Primary
```

Supported for:

* MySQL
* MariaDB
* PostgreSQL

---

### Connection Pooling

Built-in connection pooling:

* Backend connection reuse
* Pool warming
* Idle cleanup
* Pool sizing
* Health-aware pooling

---

### Query Firewall

Protect databases from dangerous operations.

Examples:

```sql
DROP DATABASE production;
```

```sql
TRUNCATE TABLE users;
```

```sql
DELETE FROM users;
```

Optional rule-based blocking.

---

### Multi-Tenant Routing

Map tenants to specific database clusters.

```text
tenant_a -> mysql-cluster-1

tenant_b -> postgres-cluster-2

tenant_c -> rds-mysql-01
```

Applications connect to DBMesh while infrastructure remains flexible.

---

## Architecture

```text
                       Clients
                           |
                           v

                  +----------------+
                  |     DBMesh     |
                  +----------------+
                           |
               +-----------+-----------+
               |                       |
         Control Plane           Data Plane
               |                       |
       +-------+------+                |
       |              |                |
       v              v                v

   Cluster      Schema Registry    Query Router
   Manager
                                        |
                                        v

                        +---------------+---------------+
                        |               |               |

                     MySQL         PostgreSQL       MariaDB
```

---

## Configuration Example

```yaml
schemas:
  billing:
    strategy: weighted

    backends:
      - host: 10.10.1.11
        port: 3306
        weight: 10

      - host: 10.10.1.12
        port: 3306
        weight: 5

  analytics:
    strategy: failover

    backends:
      - host: 10.20.1.10
        port: 5432
        role: primary

      - host: 10.20.1.11
        port: 5432
        role: replica
```

---

## Cluster Discovery

Static peers:

```yaml
cluster:
  peers:
    - 10.0.0.10
    - 10.0.0.11
    - 10.0.0.12
```

DNS discovery:

```yaml
cluster:
  seed_dns:
    - dbmesh.internal.company.com
```

---

## REST API

Examples:

```http
GET /api/v1/schemas
```

```http
GET /api/v1/backends
```

```http
GET /api/v1/nodes
```

```http
GET /api/v1/sessions
```

```http
GET /api/v1/metrics
```

---

## Management UI

Built-in web interface:

### Dashboard

* Cluster health
* Active sessions
* Query rate
* Failovers

### Schema Manager

* Add schemas
* Modify routing policies
* Backend assignments

### Backend Manager

* Health status
* Latency
* Connection counts

### Session Viewer

* Active sessions
* Current backend
* Recovery status

### Audit Logs

* Configuration changes
* User activity
* Failover events

---

## Technology Stack

### Core

* C++20
* Boost.Asio
* Boost.Beast
* Boost.JSON
* Boost.UUID

### Storage

* SQLite

### Frontend

* React
* TypeScript

### Observability

* Prometheus
* OpenTelemetry

### Build System

* CMake

---

## Performance Goals

* 100,000+ concurrent connections
* O(1) routing lookups
* Sub-second cluster configuration propagation
* Less than 100 microseconds routing overhead
* Millisecond-scale failover detection
* Zero-downtime configuration updates

---

## GOALS

* MySQL protocol support
* Schema routing
* Session routing
* Connection pooling
* REST API
* PostgreSQL support
* Failover engine
* Health monitoring
* Management UI
* Gossip clustering
* Distributed configuration
* Peer discovery
* Session recovery framework
* Read/write splitting
* Query firewall
* Metrics and observability
* Cross-database federation
* AI routing advisor
* Automatic tenant rebalancing
* Live database migration

---

## Use Cases

### SaaS Platforms

Database-per-tenant architecture.

### Managed Database Providers

Centralized routing and failover.

### Enterprise Applications

Multi-database environments.

### Cloud Migrations

Move databases without application changes.

### Hybrid Deployments

Mix on-premise and cloud databases.

---

## Vision

DBMesh aims to become the service mesh for relational databases.

One endpoint.

One routing layer.

One control plane.

Any database.

