-- DBMesh local dev database initialisation.
-- Runs once on first start of mysql-primary.

-- Replication user (used by mysql-replica to replicate from primary)
CREATE USER IF NOT EXISTS 'replicator'@'%'
  IDENTIFIED WITH mysql_native_password BY 'repl_password';
GRANT REPLICATION SLAVE ON *.* TO 'replicator'@'%';

-- DBMesh backend user (used by DBMesh to connect to backends)
CREATE USER IF NOT EXISTS 'dbmesh'@'%'
  IDENTIFIED WITH mysql_native_password BY 'dbmesh_password';
GRANT SELECT, INSERT, UPDATE, DELETE,
      CREATE, DROP, INDEX, ALTER,
      SHOW DATABASES,
      REPLICATION CLIENT, REPLICATION SLAVE
  ON *.* TO 'dbmesh'@'%';

-- Dev databases
CREATE DATABASE IF NOT EXISTS app;
CREATE DATABASE IF NOT EXISTS billing;
CREATE DATABASE IF NOT EXISTS analytics;

FLUSH PRIVILEGES;
