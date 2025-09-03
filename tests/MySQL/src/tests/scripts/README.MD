# MySQL Replication Baseline

## Machine Setup

node0: ProxySQL + Orchestrator
node1: sysbench
node2: MySQL primary (f7680e9 / 5.7)
node3: MySQL replica (f7680e9 / 5.7) (Semi-sync)

## Setup Process

1. Init data directory

```bash
sudo /usr/local/mysql/bin/mysqld --defaults-file=./`hostname -s`.cnf --initialize-insecure
```

2. Start MySQL

```bash
sudo /usr/local/mysql/bin/mysqld --defaults-file=./`hostname -s`.cnf --user=root --report-host="$(hostname)" --report-port=60000
```

3. Set up primary instance on node2

```bash
sudo /usr/local/mysql/bin/mysql --socket=/var/run/mysqld-60000.sock -e "
CREATE SCHEMA sbtest;
CREATE USER IF NOT EXISTS 'repl'@'%' IDENTIFIED BY 'STRONG_PASS';
GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%';
CREATE USER IF NOT EXISTS 'haproxy'@'%' IDENTIFIED BY 'x';
GRANT USAGE ON *.* TO 'haproxy'@'%';
CREATE USER 'sbtest'@'%' IDENTIFIED BY 'password';
GRANT ALL PRIVILEGES ON sbtest.* TO 'sbtest'@'%';
CREATE USER IF NOT EXISTS 'monitor'@'%' IDENTIFIED BY 'MONITOR_PASS';
GRANT USAGE ON *.* TO 'monitor'@'%';
GRANT REPLICATION CLIENT ON *.* TO 'monitor'@'%';
CREATE USER IF NOT EXISTS 'orchestrator'@'%' IDENTIFIED BY 'ORC_PASS';
GRANT SUPER, RELOAD, PROCESS, REPLICATION SLAVE, REPLICATION CLIENT ON *.* TO 'orchestrator'@'%';
FLUSH PRIVILEGES;"
```

```bash
sudo /usr/local/mysql/bin/mysql --socket=/var/run/mysqld-60000.sock -e "
SET GLOBAL rpl_semi_sync_master_enabled=ON;
SET GLOBAL rpl_semi_sync_master_timeout=1000;"
```

4. Set up replica instance on node3

```bash
sudo /usr/local/mysql/bin/mysql --socket=/var/run/mysqld-60000.sock -e "
SET GLOBAL rpl_semi_sync_slave_enabled=ON;"
```

```bash
sudo /usr/local/mysql/bin/mysql --socket=/var/run/mysqld-60000.sock -e "
CHANGE MASTER TO
  MASTER_HOST='node2.litesys.failuredetection-pg0.wisc.cloudlab.us', MASTER_PORT=60000,
  MASTER_USER='repl', MASTER_PASSWORD='STRONG_PASS',
  MASTER_AUTO_POSITION=1;
START SLAVE;"
```

```bash
sudo /usr/local/mysql/bin/mysql --socket=/var/run/mysqld-60000.sock -e "SHOW SLAVE STATUS\G" | egrep 'Running:|Seconds_Behind_Master|Last_.*_Error'
```

5. Set up ProxySQL on node0

```bash
sudo wget -nv -O /usr/share/keyrings/proxysql-2.7.x-keyring.gpg \
  'https://repo.proxysql.com/ProxySQL/proxysql-2.7.x/repo_pub_key.gpg'
echo "deb [signed-by=/usr/share/keyrings/proxysql-2.7.x-keyring.gpg] \
https://repo.proxysql.com/ProxySQL/proxysql-2.7.x/$(lsb_release -sc)/ ./" \
| sudo tee /etc/apt/sources.list.d/proxysql.list
sudo apt-get update
sudo apt-get install -y proxysql=2.7.3
sudo apt-mark hold proxysql
```

```bash
sudo systemctl start proxysql
/usr/local/mysql/bin/mysql  -u admin -padmin -h 127.0.0.1 -P6032 <<'SQL'
-- Backend servers (start them both in HG10; ProxySQL will move them based on read_only)
DELETE FROM mysql_servers;
INSERT INTO mysql_servers(hostgroup_id,hostname,port) VALUES
  (10,'node2',60000),
  (10,'node3',60000);

-- Replication awareness: writer HG=10, reader HG=20; follow @@read_only
DELETE FROM mysql_replication_hostgroups;
INSERT INTO mysql_replication_hostgroups (writer_hostgroup, reader_hostgroup, check_type, comment)
VALUES (10,20,'read_only','classic-repl');

-- Monitoring creds (must exist on node2 and replicate to node3)
UPDATE global_variables SET variable_value='monitor'      WHERE variable_name='mysql-monitor_username';
UPDATE global_variables SET variable_value='MONITOR_PASS' WHERE variable_name='mysql-monitor_password';

-- App user presented by ProxySQL (same creds as on MySQL)
DELETE FROM mysql_users WHERE username='sbtest';
INSERT INTO mysql_users(username,password,default_hostgroup) VALUES ('sbtest','password',10);

-- Apply & persist
LOAD MYSQL VARIABLES TO RUNTIME; SAVE MYSQL VARIABLES TO DISK;
LOAD MYSQL SERVERS   TO RUNTIME; SAVE MYSQL SERVERS   TO DISK;
LOAD MYSQL USERS     TO RUNTIME; SAVE MYSQL USERS     TO DISK;

-- Sanity
SELECT hostgroup_id,hostname,port,status FROM runtime_mysql_servers;
SQL
```

6. Set up Orchestrator on node0

```bash
ORC_VER=3.2.6
wget -nv "https://github.com/openark/orchestrator/releases/download/v${ORC_VER}/orchestrator_${ORC_VER}_amd64.deb"
sudo apt-get install -y ./orchestrator_${ORC_VER}_amd64.deb
```

```bash
sudo systemctl start orchestrator
sudo mkdir -p /var/lib/orchestrator
sudo systemctl restart orchestrator
```

```bash
export ORCHESTRATOR_API="http://127.0.0.1:3000"
/usr/local/orchestrator/resources/bin/orchestrator-client -c discover -i node2.litesys.failuredetection-pg0.wisc.cloudlab.us:60000
/usr/local/orchestrator/resources/bin/orchestrator-client -c discover -i node3.litesys.failuredetection-pg0.wisc.cloudlab.us:60000
/usr/local/orchestrator/resources/bin/orchestrator-client -c topology -i node2.litesys.failuredetection-pg0.wisc.cloudlab.us:60000
```

## Test Process

1. Run sysbench

```bash
sysbench \
./oltp_read_write.lua \
--db-driver=mysql \
--report-interval=1 \
--tables=1 \
--table-size=100000 \
--threads=8 --rate=500 \
--time=0 \
--mysql-host=node0 \
--mysql-port=6033 \
--mysql-user=sbtest \
--mysql-password=password \
--db-ps-mode=disable \
--mysql-ignore-errors=2013,1062,2027 \
--skip_trx=on --rand-type=zipfian --rand-zipfian-exp=1 \
prepare
```

```bash
sysbench \
./oltp_read_write.lua \
--db-driver=mysql \
--report-interval=1 \
--tables=1 \
--table-size=100000 \
--threads=8 --rate=500 \
--time=0 \
--mysql-host=node0 \
--mysql-port=6033 \
--mysql-user=sbtest \
--mysql-password=password \
--db-ps-mode=disable \
--mysql-ignore-errors=2013,1062,2027 \
--skip_trx=on --rand-type=zipfian --rand-zipfian-exp=1 \
run
```

2. Kill the primary instance

```bash
# sudo journalctl -u orchestrator -f
```

3. Recover the primary instance

3.1 Keep the primary instance up-to-date on node2

```bash
sudo /usr/local/mysql/bin/mysql --socket=/var/run/mysqld-60000.sock -e "
SET GLOBAL read_only=ON; SET GLOBAL super_read_only=ON;
CHANGE MASTER TO
  MASTER_HOST='node3.litesys.failuredetection-pg0.wisc.cloudlab.us',
  MASTER_PORT=60000,
  MASTER_USER='repl', MASTER_PASSWORD='STRONG_PASS',
  MASTER_AUTO_POSITION=1;
START SLAVE;
SET GLOBAL rpl_semi_sync_slave_enabled=ON;"
# watch it catch up
sudo /usr/local/mysql/bin/mysql --socket=/var/run/mysqld-60000.sock -e "SHOW SLAVE STATUS\G" | egrep 'Seconds_Behind_Master|Running:'
```

3.2 Graceful Master Takeover by Orchestrator on node0

```bash
/usr/local/orchestrator/resources/bin/orchestrator-client \
  -c graceful-master-takeover \
  -i node3.litesys.failuredetection-pg0.wisc.cloudlab.us:60000 \
  -d node2.litesys.failuredetection-pg0.wisc.cloudlab.us:60000 \
  -r "switch back to node2"
/usr/local/orchestrator/resources/bin/orchestrator-client \
  -c end-downtime -i node3.litesys.failuredetection-pg0.wisc.cloudlab.us:60000
```

3.3 Set up the replica instance on node3

```bash
sudo /usr/local/mysql/bin/mysql --socket=/var/run/mysqld-60000.sock -e "
STOP SLAVE;
RESET SLAVE ALL;
SET GLOBAL read_only=ON; SET GLOBAL super_read_only=ON;
CHANGE MASTER TO
  MASTER_HOST='node2.litesys.failuredetection-pg0.wisc.cloudlab.us',
  MASTER_PORT=60000,
  MASTER_USER='repl', MASTER_PASSWORD='STRONG_PASS',
  MASTER_AUTO_POSITION=1;
START SLAVE;
SET GLOBAL rpl_semi_sync_slave_enabled=ON;"
```