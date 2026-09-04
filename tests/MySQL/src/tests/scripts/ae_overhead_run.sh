#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/mysql-overhead/$(date +%Y%m%d-%H%M%S)}
REPEATS=${AE_REPEATS:-3}
CASE_ATTEMPTS=${AE_CASE_ATTEMPTS:-3}
DURATION=${AE_DURATION:-60}
WARMUP_DURATION=${AE_WARMUP_DURATION:-60}
TABLE_SIZE=${AE_TABLE_SIZE:-100000}
THREADS=${AE_THREADS:-8}
# The replacement cluster's NDB paths need a higher offered rate to expose
# the distributed latency visible in the paper without saturating the proxy.
RATE=${AE_RATE:-1250}
SEMISYNC=${AE_SEMISYNC:-1}
REPLICA_CONNECTION=${AE_REPLICA_CONNECTION:-proxy}
MONITOR_SECONDS=$((DURATION + 5))
MODES=${AE_MODES:-"full proxy replica ndb-client ndb-proxy"}
RUNTIME=/tmp/litelib-ae-mysql
mkdir -p "${OUT}"
cat >"${OUT}/metadata.json" <<EOF
{
  "classic_durability": {
    "innodb_flush_log_at_trx_commit": 1,
    "sync_binlog": 1,
    "binlog_format": "ROW"
  },
  "replica_connection": "${REPLICA_CONNECTION}",
  "semisync": ${SEMISYNC},
  "ndb_proxy_connection": "raw",
  "workload": {
    "duration_seconds": ${DURATION},
    "warmup_seconds": ${WARMUP_DURATION},
    "table_size": ${TABLE_SIZE},
    "threads": ${THREADS},
    "rate": ${RATE}
  }
}
EOF

node_cmd() {
  local node=$1
  shift
  ssh "${node}" "'${REMOTE}/tests/MySQL/src/tests/scripts/ae_overhead_node.sh' $*"
}

cleanup_nodes() {
  for node in node0 node1 node2 node3; do
    node_cmd "${node}" cleanup || true
  done
}

trap cleanup_nodes EXIT

run_sysbench() {
  local action=$1 connection=$2 host=$3 port=$4 engine=$5 log=$6
  local run_duration=${7:-${DURATION}}
  ssh node1 bash -s -- "${action}" "${connection}" "${host}" "${port}" \
    "${engine}" "${run_duration}" "${TABLE_SIZE}" "${RATE}" "${THREADS}" <<'REMOTE_SCRIPT' \
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
threads=$9
args=(
  /usr/local/bin/sysbench
  /usr/local/share/sysbench/oltp_read_write.lua
  --db-driver=mysql
  --report-interval=1
  --tables=1
  --table-size="${table_size}"
  --threads="${threads}"
  --rate="${rate}"
  --time="${duration}"
  --mysql-user=sbtest
  --mysql-password=password
  --mysql-db=sbtest
  --db-ps-mode=disable
  --mysql-ignore-errors=1053,2013,1062,2027
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

warmup_sysbench() {
  local connection=$1 host=$2 port=$3 engine=$4 log=$5
  [ "${WARMUP_DURATION}" -gt 0 ] || return 0
  run_sysbench run "${connection}" "${host}" "${port}" "${engine}" \
    "${log}" "${WARMUP_DURATION}"
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
  echo "classic MySQL replica did not synchronize" >&2
  return 1
}

wait_semisync() {
  local prefix=$1
  local socket="${RUNTIME}/${prefix}/classic/mysql.sock"
  for _ in $(seq 1 600); do
    if node_cmd node2 check-semisync-primary "${prefix}" "${socket}"; then
      return
    fi
    sleep 0.2
  done
  echo "classic MySQL semi-synchronous replication is inactive" >&2
  return 1
}

configure_proxysql() {
  local mode=$1
  ssh node0 "sudo -n systemctl restart proxysql"
  for _ in $(seq 1 300); do
    if ssh node0 "${HOME}/mysql-ae/bin/mysql -uadmin -padmin \
        -h127.0.0.1 -P6032 -e 'SELECT 1' >/dev/null 2>&1"; then
      break
    fi
    sleep 0.1
  done
  if [ "${mode}" = replica ]; then
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
      sudo -n cp '${REMOTE}/tests/MySQL/src/tests/scripts/orchestrator.conf.json' /etc/orchestrator.conf.json &&
      sudo -n systemctl restart orchestrator"
    ssh node0 "/usr/local/orchestrator/resources/bin/orchestrator-client \
      -c discover -i node2:60000 >/dev/null 2>&1 || true"
  else
    ssh node0 "${HOME}/mysql-ae/bin/mysql -uadmin -padmin -h127.0.0.1 -P6032" <<'SQL'
DELETE FROM mysql_servers;
INSERT INTO mysql_servers(hostgroup_id,hostname,port) VALUES
  (10,'node2',50000),(10,'node3',50000);
DELETE FROM mysql_replication_hostgroups;
DELETE FROM mysql_users;
INSERT INTO mysql_users(username,password,default_hostgroup)
VALUES('sbtest','password',10);
DELETE FROM mysql_query_rules;
DELETE FROM scheduler;
UPDATE global_variables SET variable_value='0'
  WHERE variable_name='mysql-eventslog_default_log';
UPDATE global_variables SET variable_value='monitor'
  WHERE variable_name='mysql-monitor_username';
UPDATE global_variables SET variable_value='MONITOR_PASS'
  WHERE variable_name='mysql-monitor_password';
LOAD MYSQL VARIABLES TO RUNTIME; SAVE MYSQL VARIABLES TO DISK;
LOAD MYSQL SERVERS TO RUNTIME; SAVE MYSQL SERVERS TO DISK;
LOAD MYSQL USERS TO RUNTIME; SAVE MYSQL USERS TO DISK;
LOAD MYSQL QUERY RULES TO RUNTIME; SAVE MYSQL QUERY RULES TO DISK;
SQL
  fi
}

start_monitors() {
  local prefix=$1
  shift
  for node in "$@"; do
    node_cmd "${node}" start-monitor "${MONITOR_SECONDS}" "${prefix}"
  done
}

collect_monitors() {
  local prefix=$1
  shift
  for node in "$@"; do
    node_cmd "${node}" wait-monitor "${prefix}"
    scp -q "${node}:${RUNTIME}/${prefix}/monitor/monitor.jsonl" \
      "${OUT}/monitor-${node}-${prefix}.jsonl"
  done
}

start_classic_pair() {
  local prefix=$1
  local socket="${RUNTIME}/${prefix}/classic/mysql.sock"
  node_cmd node2 start-classic "${prefix}" primary 2 60000 "${socket}"
  node_cmd node2 setup-primary "${prefix}" "${socket}"
  node_cmd node3 start-classic "${prefix}" replica 3 60000 "${socket}"
  node_cmd node3 setup-replica "${prefix}" "${socket}"
  wait_replica "${prefix}"
  if [ "${SEMISYNC}" -eq 1 ]; then
    node_cmd node2 enable-semisync-primary "${prefix}" "${socket}"
    wait_semisync "${prefix}"
  fi
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

run_one() {
  local mode=$1 rep=$2 prefix="${mode}-${rep}"
  local log="${OUT}/sysbench-${prefix}.log"
  cleanup_nodes
  case "${mode}" in
  full)
    socket="${RUNTIME}/${prefix}/classic/mysql.sock"
    node_cmd node2 start-classic "${prefix}" standalone 2 60000 "${socket}"
    node_cmd node2 setup-primary "${prefix}" "${socket}"
    run_sysbench prepare direct node2 60000 innodb "${OUT}/prepare-${prefix}.log"
    warmup_sysbench direct node2 60000 innodb "${OUT}/warmup-${prefix}.log"
    start_monitors "${prefix}" node2
    run_sysbench run direct node2 60000 innodb "${log}"
    collect_monitors "${prefix}" node2
    ;;
  proxy)
    node_cmd node0 start-classic "${prefix}" standalone 1 60000 /tmp/mysql.sock
    node_cmd node0 setup-primary "${prefix}" /tmp/mysql.sock
    node_cmd node0 start-lite "${prefix}"
    run_sysbench prepare direct node0 60000 innodb "${OUT}/prepare-${prefix}.log"
    warmup_sysbench direct node0 59999 innodb "${OUT}/warmup-${prefix}.log"
    start_monitors "${prefix}" node0
    run_sysbench run direct node0 59999 innodb "${log}"
    collect_monitors "${prefix}" node0
    ;;
  replica)
    start_classic_pair "${prefix}"
    run_sysbench prepare direct node2 60000 innodb "${OUT}/prepare-${prefix}.log"
    wait_replica "${prefix}"
    if [ "${REPLICA_CONNECTION}" = proxy ]; then
      configure_proxysql replica
      warmup_sysbench direct node0 6033 innodb "${OUT}/warmup-${prefix}.log"
    else
      warmup_sysbench direct node2 60000 innodb "${OUT}/warmup-${prefix}.log"
    fi
    wait_replica "${prefix}"
    start_monitors "${prefix}" node0 node2 node3
    if [ "${REPLICA_CONNECTION}" = proxy ]; then
      run_sysbench run direct node0 6033 innodb "${log}"
    else
      run_sysbench run direct node2 60000 innodb "${log}"
    fi
    collect_monitors "${prefix}" node0 node2 node3
    ;;
  ndb-client|ndb-proxy)
    start_ndb "${prefix}"
    run_sysbench prepare direct node2 50000 ndbcluster \
      "${OUT}/prepare-${prefix}.log"
    if [ "${mode}" = ndb-proxy ]; then configure_proxysql ndb; fi
    if [ "${mode}" = ndb-client ]; then
      warmup_sysbench raw "10.10.1.3:50000,10.10.1.4:50000" 0 \
        ndbcluster "${OUT}/warmup-${prefix}.log"
    else
      warmup_sysbench raw "10.10.1.1:6033" 0 ndbcluster \
        "${OUT}/warmup-${prefix}.log"
    fi
    start_monitors "${prefix}" node0 node2 node3
    if [ "${mode}" = ndb-client ]; then
      run_sysbench run raw "10.10.1.3:50000,10.10.1.4:50000" 0 \
        ndbcluster "${log}"
    else
      run_sysbench run raw "10.10.1.1:6033" 0 ndbcluster "${log}"
    fi
    collect_monitors "${prefix}" node0 node2 node3
    ;;
  esac
}

if [ -n "${AE_SINGLE_MODE:-}" ]; then
  echo "==> ${AE_SINGLE_MODE} ${AE_SINGLE_REP}/${REPEATS}"
  run_one "${AE_SINGLE_MODE}" "${AE_SINGLE_REP}"
  exit 0
fi

for rep in $(seq 1 "${REPEATS}"); do
  for mode in ${MODES}; do
    success=0
    for attempt in $(seq 1 "${CASE_ATTEMPTS}"); do
      echo "==> ${mode} ${rep}/${REPEATS}, attempt ${attempt}/${CASE_ATTEMPTS}"
      if AE_OUTPUT_DIR="${OUT}" \
        AE_REPEATS="${REPEATS}" AE_CASE_ATTEMPTS="${CASE_ATTEMPTS}" \
        AE_DURATION="${DURATION}" AE_WARMUP_DURATION="${WARMUP_DURATION}" \
        AE_TABLE_SIZE="${TABLE_SIZE}" AE_THREADS="${THREADS}" AE_RATE="${RATE}" \
        AE_SEMISYNC="${SEMISYNC}" AE_REPLICA_CONNECTION="${REPLICA_CONNECTION}" \
        AE_MODES="${MODES}" AE_SINGLE_MODE="${mode}" AE_SINGLE_REP="${rep}" \
        "${BASH_SOURCE[0]}"; then
        success=1
        break
      fi
    done
    if [ "${success}" -ne 1 ]; then
      echo "${mode} ${rep}/${REPEATS} failed after ${CASE_ATTEMPTS} attempts" >&2
      exit 1
    fi
  done
done
echo "  [ OK ] MySQL raw output -> ${OUT}"
