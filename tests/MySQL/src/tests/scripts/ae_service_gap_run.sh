#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/table2/mysql-$(date +%Y%m%d-%H%M%S)}
REPEATS=${AE_REPEATS:-3}
CRASH_AFTER=${AE_CRASH_AFTER:-30}
DURATION=${AE_DURATION:-70}
TABLE_SIZE=${AE_TABLE_SIZE:-100000}
AP_RATE=${AE_AP_RATE:-500}
AP_DETECTION_SECONDS=${AE_AP_DETECTION_SECONDS:-3}
NDB_RATE=${AE_NDB_RATE:-0}
MODES=${AE_MODES:-"ap ndb-client ndb-proxy lite-proxy"}
RUNTIME=/tmp/litelib-ae-mysql
mkdir -p "${OUT}"
[ "${TABLE_SIZE}" -eq 100000 ] || {
  echo "[FAIL] Table 2 MySQL runs require the full 100,000-row dataset" >&2
  exit 2
}

node_cmd() {
  local node=$1
  shift
  ssh "${node}" "'${REMOTE}/tests/MySQL/src/tests/scripts/ae_overhead_node.sh' $*"
}

cleanup_nodes() {
  for node in node0 node1 node2 node3; do
    node_cmd "${node}" cleanup || true
  done
  ssh node2 "sudo -n iptables -D INPUT -p tcp --dport 50000 \
    -m comment --comment litelib-table2-ndb -j DROP" \
    >/dev/null 2>&1 || true
}
trap cleanup_nodes EXIT

run_sysbench() {
  local action=$1 connection=$2 host=$3 port=$4 engine=$5 rate=$6 log=$7
  ssh node1 bash -s -- "${action}" "${connection}" "${host}" "${port}" \
    "${engine}" "${DURATION}" "${TABLE_SIZE}" "${rate}" <<'REMOTE_SCRIPT' \
    >"${log}" 2>&1
set -euo pipefail
action=$1
connection=$2
host=$3
port=$4
engine=$5
duration=$6
table_size=$7
rate=$8
args=(
  /usr/local/bin/sysbench
  /usr/local/share/sysbench/oltp_read_write.lua
  --db-driver=mysql
  --report-interval=1
  --tables=1
  --table-size="${table_size}"
  --threads=8
  --rate="${rate}"
  --time="${duration}"
  --mysql-user=sbtest
  --mysql-password=password
  --mysql-db=sbtest
  --db-ps-mode=disable
  --mysql-ignore-errors=1053,2002,2013,1062,2027,9001
  --skip_trx=on
  --rand-type=zipfian
  --rand-zipfian-exp=1
)
if [ "${connection}" = raw ]; then
  args+=(--mysql-host-raw="${host}")
else
  args+=(--mysql-host="${host}" --mysql-port="${port}")
fi
if [ "${engine}" = ndbcluster ]; then
  args+=(--mysql-storage-engine=ndbcluster)
fi
"${args[@]}" "${action}"
REMOTE_SCRIPT
}

wait_replica() {
  local prefix=$1
  for _ in $(seq 1 600); do
    status=$(ssh node3 "${HOME}/mysql-ae/bin/mysql \
      --socket='${RUNTIME}/${prefix}/classic/mysql.sock' -uroot \
      -e 'SHOW SLAVE STATUS\\G'" 2>/dev/null || true)
    if grep -q 'Slave_IO_Running: Yes' <<<"${status}" &&
       grep -q 'Slave_SQL_Running: Yes' <<<"${status}" &&
       grep -q 'Seconds_Behind_Master: 0' <<<"${status}"; then
      return
    fi
    sleep 0.2
  done
  return 1
}

configure_ap() {
  ssh node0 "sudo -n systemctl restart proxysql"
  for _ in $(seq 1 300); do
    ssh node0 "${HOME}/mysql-ae/bin/mysql -uadmin -padmin \
      -h127.0.0.1 -P6032 -e 'SELECT 1' >/dev/null 2>&1" && break
    sleep 0.1
  done
  ssh node0 "${HOME}/mysql-ae/bin/mysql -uadmin -padmin -h127.0.0.1 -P6032" <<'SQL'
DELETE FROM mysql_servers;
INSERT INTO mysql_servers(hostgroup_id,hostname,port) VALUES
  (10,'node2',60000),(10,'node3',60000);
DELETE FROM mysql_replication_hostgroups;
INSERT INTO mysql_replication_hostgroups(writer_hostgroup,reader_hostgroup,check_type,comment)
VALUES(10,20,'read_only','classic-repl');
DELETE FROM mysql_users;
INSERT INTO mysql_users(username,password,default_hostgroup)
VALUES('sbtest','password',10);
DELETE FROM mysql_query_rules;
UPDATE global_variables SET variable_value='monitor'
  WHERE variable_name='mysql-monitor_username';
UPDATE global_variables SET variable_value='MONITOR_PASS'
  WHERE variable_name='mysql-monitor_password';
LOAD MYSQL VARIABLES TO RUNTIME; SAVE MYSQL VARIABLES TO DISK;
LOAD MYSQL SERVERS TO RUNTIME; SAVE MYSQL SERVERS TO DISK;
LOAD MYSQL USERS TO RUNTIME; SAVE MYSQL USERS TO DISK;
LOAD MYSQL QUERY RULES TO RUNTIME; SAVE MYSQL QUERY RULES TO DISK;
SQL
  ssh node0 "sudo -n mkdir -p /var/lib/orchestrator &&
    sudo -n cp '${REMOTE}/tests/MySQL/src/tests/scripts/ae_orchestrator.conf.json' /etc/orchestrator.conf.json &&
    sudo -n systemctl restart orchestrator"
  ssh node0 "/usr/local/orchestrator/resources/bin/orchestrator-client \
    -c discover -i node2:60000 >/dev/null"
}

start_classic_pair() {
  local prefix=$1
  local socket="${RUNTIME}/${prefix}/classic/mysql.sock"
  node_cmd node2 start-classic "${prefix}" primary 2 60000 "${socket}" node2
  node_cmd node2 setup-primary "${prefix}" "${socket}"
  node_cmd node3 start-classic "${prefix}" replica 3 60000 "${socket}" node3
  node_cmd node3 setup-replica "${prefix}" "${socket}"
  wait_replica "${prefix}"
}

start_ndb() {
  local prefix=$1
  node_cmd node0 start-ndb-mgmd "${prefix}"
  sleep 2
  node_cmd node2 start-ndbd "${prefix}" 2 &
  p2=$!
  node_cmd node3 start-ndbd "${prefix}" 3 &
  p3=$!
  wait "${p2}" "${p3}"
  for _ in $(seq 1 600); do
    show=$(ssh node0 "/opt/mysql-ndb/bin/ndb_mgm -e show" 2>/dev/null || true)
    [ "$(grep -c 'Nodegroup: 0' <<<"${show}")" -ge 2 ] && break
    sleep 0.2
  done
  node_cmd node2 start-ndb-sql "${prefix}" 50 &
  p2=$!
  node_cmd node3 start-ndb-sql "${prefix}" 51 &
  p3=$!
  wait "${p2}" "${p3}"
  node_cmd node2 setup-ndb-sql "${prefix}" yes
  node_cmd node3 setup-ndb-sql "${prefix}" no
}

configure_ndb_proxy() {
  ssh node0 "sudo -n rm -f /var/lib/proxysql/queries.log.* &&
    sudo -n systemctl restart proxysql"
  for _ in $(seq 1 300); do
    ssh node0 "${HOME}/mysql-ae/bin/mysql -uadmin -padmin \
      -h127.0.0.1 -P6032 -e 'SELECT 1' >/dev/null 2>&1" && break
    sleep 0.1
  done
  ssh node0 "${HOME}/mysql-ae/bin/mysql -uadmin -padmin -h127.0.0.1 -P6032" <<'SQL'
DELETE FROM mysql_servers;
INSERT INTO mysql_servers(hostgroup_id,hostname,port,weight) VALUES
  (10,'node2',50000,100000),(10,'node3',50000,1);
DELETE FROM mysql_users;
INSERT INTO mysql_users(username,password,default_hostgroup)
VALUES('sbtest','password',10);
DELETE FROM mysql_query_rules;
UPDATE global_variables SET variable_value='1'
  WHERE variable_name='mysql-connect_retries_on_failure';
UPDATE global_variables SET variable_value='25'
  WHERE variable_name='mysql-connect_timeout_server';
UPDATE global_variables SET variable_value='1'
  WHERE variable_name='mysql-eventslog_default_log';
UPDATE global_variables SET variable_value='2'
  WHERE variable_name='mysql-eventslog_format';
UPDATE global_variables SET variable_value='queries.log'
  WHERE variable_name='mysql-eventslog_filename';
UPDATE global_variables SET variable_value='104857600'
  WHERE variable_name='mysql-eventslog_filesize';
LOAD MYSQL VARIABLES TO RUNTIME; SAVE MYSQL VARIABLES TO DISK;
LOAD MYSQL SERVERS TO RUNTIME; SAVE MYSQL SERVERS TO DISK;
LOAD MYSQL USERS TO RUNTIME; SAVE MYSQL USERS TO DISK;
SQL
}

kill_mysql() {
  local node=$1 prefix=$2 kind=$3 output=$4 drop=${5:-no}
  ssh "${node}" bash -s -- "${prefix}" "${kind}" "${drop}" <<'REMOTE_SCRIPT' >"${output}"
set -euo pipefail
prefix=$1
kind=$2
drop=$3
pid=$(cat "/tmp/litelib-ae-mysql/${prefix}/${kind}/mysqld.pid")
t0=$(date +%s%6N)
if [ "${drop}" = yes ]; then
  sudo -n iptables -I INPUT 1 -p tcp --dport 50000 \
    -m comment --comment litelib-table2-ndb -j DROP
fi
kill -9 "${pid}"
t1=$(date +%s%6N)
while kill -0 "${pid}" 2>/dev/null; do :; done
tdead=$(date +%s%6N)
echo "t0=${t0} t1=${t1} tdead=${tdead} pid=${pid}"
REMOTE_SCRIPT
}

run_ap() {
  local rep=$1
  local prefix="ap-${rep}"
  cleanup_nodes
  start_classic_pair "${prefix}"
  run_sysbench prepare direct node2 60000 innodb "${AP_RATE}" \
    "${OUT}/prepare-${prefix}.log"
  wait_replica "${prefix}"
  configure_ap
  run_sysbench run direct node0 6033 innodb "${AP_RATE}" \
    "${OUT}/sysbench-${prefix}.log" &
  bench_pid=$!
  sleep "${CRASH_AFTER}"
  kill_mysql node2 "${prefix}" classic "${OUT}/kill-${prefix}.txt"
  sleep "${AP_DETECTION_SECONDS}"
  ssh node3 "${HOME}/mysql-ae/bin/mysql \
    --socket='${RUNTIME}/${prefix}/classic/mysql.sock' -uroot" <<'SQL'
STOP SLAVE;
RESET SLAVE ALL;
SET GLOBAL read_only=OFF;
SET GLOBAL super_read_only=OFF;
SQL
  for _ in $(seq 1 300); do
    status=$(ssh node0 "${HOME}/mysql-ae/bin/mysql -uadmin -padmin \
      -h127.0.0.1 -P6032 -N -e \
      \"SELECT COUNT(*) FROM runtime_mysql_servers
       WHERE hostgroup_id=10 AND hostname='node3' AND status='ONLINE'\"" \
      2>/dev/null || true)
    [ "${status}" = 1 ] && break
    sleep 0.1
  done
  wait "${bench_pid}"
}

run_ndb_client() {
  local rep=$1
  local prefix="ndb-client-${rep}"
  cleanup_nodes
  start_ndb "${prefix}"
  run_sysbench prepare direct node2 50000 ndbcluster "${AP_RATE}" \
    "${OUT}/prepare-${prefix}.log"
  "${ROOT}/scripts/probe_timing_offset.sh" node2 >"${OUT}/offset-${prefix}.txt"
  run_sysbench run raw "10.10.1.3:50000,10.10.1.4:50000" 0 \
    ndbcluster "${NDB_RATE}" "${OUT}/sysbench-${prefix}.log" &
  bench_pid=$!
  sleep "${CRASH_AFTER}"
  kill_mysql node2 "${prefix}" ndb-sql "${OUT}/kill-${prefix}.txt"
  wait "${bench_pid}"
}

run_ndb_proxy() {
  local rep=$1
  local prefix="ndb-proxy-${rep}"
  cleanup_nodes
  start_ndb "${prefix}"
  run_sysbench prepare direct node2 50000 ndbcluster "${AP_RATE}" \
    "${OUT}/prepare-${prefix}.log"
  configure_ndb_proxy
  run_sysbench run direct node0 6033 ndbcluster "${NDB_RATE}" \
    "${OUT}/sysbench-${prefix}.log" &
  bench_pid=$!
  sleep "${CRASH_AFTER}"
  kill_mysql node2 "${prefix}" ndb-sql "${OUT}/kill-${prefix}.txt" yes
  wait "${bench_pid}"
  ssh node2 "sudo -n iptables -D INPUT -p tcp --dport 50000 \
    -m comment --comment litelib-table2-ndb -j DROP"
  ssh node0 "sudo -n cat /var/lib/proxysql/queries.log.00000001" \
    >"${OUT}/queries-${prefix}.jsonl"
}

run_lite_proxy() {
  local rep=$1
  local prefix="lite-proxy-${rep}"
  cleanup_nodes
  node_cmd node0 start-classic "${prefix}" standalone 1 60000 /tmp/mysql.sock
  node_cmd node0 setup-primary "${prefix}" /tmp/mysql.sock
  node_cmd node0 start-lite "${prefix}"
  run_sysbench prepare direct node0 60000 innodb "${AP_RATE}" \
    "${OUT}/prepare-${prefix}.log"
  run_sysbench run direct node0 59999 innodb "${AP_RATE}" \
    "${OUT}/sysbench-${prefix}.log" &
  bench_pid=$!
  sleep "${CRASH_AFTER}"
  ssh node0 "'${REMOTE}/tests/MySQL/src/lite-version/build/Lite/lite_cli' \
      -t /tmp/lite_mysql -p 60000 -m 1;
    for i in \$(seq 1 300); do
      grep -q 'Entered emergency mode' \
        '${RUNTIME}/${prefix}/lite/lite.log' && break;
      sleep 0.01;
    done;
    pid=\$(cat '${RUNTIME}/${prefix}/classic/mysqld.pid');
    kill -11 \"\${pid}\"; while kill -0 \"\${pid}\" 2>/dev/null; do :; done"
  wait "${bench_pid}" || true
  scp -q "node0:${RUNTIME}/${prefix}/lite/lite.log" \
    "${OUT}/lite-${prefix}.log"
}

for rep in $(seq 1 "${REPEATS}"); do
  for mode in ${MODES}; do
    case "${mode}" in
    ap)
      echo "==> MySQL active-passive ${rep}/${REPEATS}"
      run_ap "${rep}"
      ;;
    ndb-client)
      echo "==> MySQL NDB client failover ${rep}/${REPEATS}"
      run_ndb_client "${rep}"
      ;;
    ndb-proxy)
      echo "==> MySQL NDB proxy failover ${rep}/${REPEATS}"
      run_ndb_proxy "${rep}"
      ;;
    lite-proxy)
      echo "==> MySQL LiteLib proxy ${rep}/${REPEATS}"
      run_lite_proxy "${rep}"
      ;;
    esac
  done
done

python3 "${SCRIPT_DIR}/ae_service_gap_collect.py" "${OUT}" \
  --output "${OUT}/mysql.csv"
echo "  [ OK ] MySQL Table 2 results -> ${OUT}/mysql.csv"
