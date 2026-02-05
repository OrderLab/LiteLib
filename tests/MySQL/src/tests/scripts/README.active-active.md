# MySQL NDB Cluster Baseline

## Machine Setup

(mysql-5.7.52 ndb-7.6.36)
node0: NDB Management node (ndb_mgmd) + ProxySQL
node1: sysbench client
node2: NDB Data node (ndbd) + SQL node (mysqld-ndb)
node3: NDB Data node (ndbd) + SQL node (mysqld-ndb)

## Install NDB Cluster binaries (node0, node2, node3)

```bash
sudo apt-get install -y libncurses5
wget https://cdn.mysql.com//Downloads/MySQL-Cluster-7.6/mysql-cluster-gpl-7.6.36-linux-glibc2.17-x86_64.tar.gz
sudo mkdir -p /opt/mysql-ndb
sudo tar -xavf ./mysql-cluster-gpl-7.6.36-linux-glibc2.17-x86_64.tar.gz -C /opt/mysql-ndb --strip-components=1
sudo mkdir -p /etc/mysql-ndb
sudo mkdir -p /var/lib/mysql-ndb/{mgm,ndbd2,ndbd3,mysqld2,mysqld3}
sudo mkdir -p /var/run/mysql-ndb
```

## Configure NDB on node0

```bash
sudo sh -c "echo '[NDBD DEFAULT]
NoOfReplicas=2
DataMemory=1024M
IndexMemory=256M

[TCP DEFAULT]

[NDB_MGMD]
NodeId=1
HostName=node0
DataDir=/var/lib/mysql-ndb/mgm

[NDBD]
NodeId=2
HostName=node2
DataDir=/var/lib/mysql-ndb/ndbd2

[NDBD]
NodeId=3
HostName=node3
DataDir=/var/lib/mysql-ndb/ndbd3

[MYSQLD]
NodeId=50
HostName=node2

[MYSQLD]
NodeId=51
HostName=node3
' > /etc/mysql-ndb/config.ini"
sudo /opt/mysql-ndb/bin/ndb_mgmd \
  --config-file=/etc/mysql-ndb/config.ini \
  --configdir=/var/lib/mysql-ndb/mgm \
  --ndb-nodeid=1 \
  --initial
```

```bash
/opt/mysql-ndb/bin/ndb_mgm -e "show"
```

## Configure NDB on node2 and node3

```bash
cd /var/lib/mysql-ndb/ndbd`hostname -s | sed 's/[^0-9]*//g'`
sudo /opt/mysql-ndb/bin/ndbd \
  --ndb-nodeid=`hostname -s | sed 's/[^0-9]*//g'` \
  --connect-string=node0:1186
```

```bash
sudo tee /etc/mysql-ndb/mysqld.cnf > /dev/null <<EOF
[mysqld]
basedir=/opt/mysql-ndb
datadir=/var/lib/mysql-ndb/mysqld.$(hostname -s | sed 's/[^0-9]*//g')
port=50000
socket=/var/run/mysqld-ndb-50000.sock
pid-file=/var/run/mysql-ndb/mysqld-50000.pid

query_cache_type=ON

ndbcluster
ndb-connectstring=node0:1186
skip-name-resolve
EOF
sudo /opt/mysql-ndb/bin/mysqld \
  --defaults-file=/etc/mysql-ndb/mysqld.cnf --initialize-insecure
```

```bash
sudo /opt/mysql-ndb/bin/mysqld \
  --defaults-file=/etc/mysql-ndb/mysqld.cnf --user=root
```

## Schema and user setup (node2)

```bash
sudo /opt/mysql-ndb/bin/mysql \
  --socket=/var/run/mysqld-ndb-50000.sock -e "
CREATE DATABASE sbtest;
```

## User setup (node2 and node3)
```bash
sudo /opt/mysql-ndb/bin/mysql \
  --socket=/var/run/mysqld-ndb-50000.sock -e "
CREATE USER 'sbtest'@'%' IDENTIFIED BY 'password';
GRANT ALL PRIVILEGES ON sbtest.* TO 'sbtest'@'%';
CREATE USER IF NOT EXISTS 'monitor'@'%' IDENTIFIED BY 'MONITOR_PASS';
GRANT USAGE ON *.* TO 'monitor'@'%';
FLUSH PRIVILEGES;
"
```

## Setup ProxySQL on node0

```bash
mysql -u admin -padmin -h 127.0.0.1 -P6032 <<'SQL'
-- wipe all MySQL-related state from previous experiments
DELETE FROM mysql_servers;
DELETE FROM mysql_users;
DELETE FROM mysql_query_rules;
DELETE FROM mysql_replication_hostgroups;
DELETE FROM scheduler;

-- disable events/query logging (revert to clean baseline)
UPDATE global_variables SET variable_value='0'
  WHERE variable_name='mysql-eventslog_default_log';

-- apply immediately
LOAD MYSQL SERVERS TO RUNTIME;
LOAD MYSQL USERS TO RUNTIME;
LOAD MYSQL QUERY RULES TO RUNTIME;
LOAD MYSQL VARIABLES TO RUNTIME;

-- persist clean state
SAVE MYSQL SERVERS TO DISK;
SAVE MYSQL USERS TO DISK;
SAVE MYSQL QUERY RULES TO DISK;
SAVE MYSQL VARIABLES TO DISK;
SQL
```

```bash
sudo systemctl start proxysql
mysql -u admin -padmin -h 127.0.0.1 -P6032 <<'SQL'
DELETE FROM mysql_servers;
INSERT INTO mysql_servers(hostgroup_id,hostname,port) VALUES
  (10,'node2',50000),
  (10,'node3',50000);

--- if it's for service gap:
--- DELETE FROM mysql_servers;
--- INSERT INTO mysql_servers(hostgroup_id,hostname,port,weight) VALUES
---   (10,'node2',50000, 100000),
---   (10,'node3',50000, 0);
--- UPDATE global_variables
--- SET variable_value='0'
--- WHERE variable_name='mysql-connect_retries_on_failure';

DELETE FROM mysql_users WHERE username='sbtest';
INSERT INTO mysql_users(username,password,default_hostgroup)
VALUES ('sbtest','password',10);

LOAD MYSQL SERVERS TO RUNTIME; SAVE MYSQL SERVERS TO DISK;
LOAD MYSQL USERS   TO RUNTIME; SAVE MYSQL USERS   TO DISK;

UPDATE global_variables SET variable_value='monitor'
  WHERE variable_name='mysql-monitor_username';
UPDATE global_variables SET variable_value='MONITOR_PASS'
  WHERE variable_name='mysql-monitor_password';

-- for service gap measurement only!
-- events/query log settings (JSON lines)
UPDATE global_variables SET variable_value='1'
  WHERE variable_name='mysql-eventslog_default_log';
UPDATE global_variables SET variable_value='2'
  WHERE variable_name='mysql-eventslog_format';          -- 2 = JSON
UPDATE global_variables SET variable_value='queries.log'
  WHERE variable_name='mysql-eventslog_filename';
UPDATE global_variables SET variable_value='104857600'
  WHERE variable_name='mysql-eventslog_filesize';        -- 100MB per file

LOAD MYSQL VARIABLES TO RUNTIME; SAVE MYSQL VARIABLES TO DISK;
SQL
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
--skip_trx=on --rand-type=zipfian --rand-zipfian-exp=1 --histogram \
run
```

2. Kill the primary instance

```bash
date +%s%6N; sudo pkill -f mysqld
```

3. Get the service gap in node0

```bash
sudo python ./ndb_service_gap.py --kill-us $kill_us # the time reported by date +%s%6N
sudo python ./ndb_service_impact.py --kill-us $kill_us
sudo rm /var/lib/proxysql/queries.log.*
sudo systemctl restart proxysql
```