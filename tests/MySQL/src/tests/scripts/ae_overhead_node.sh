#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MYSQL_HOME="${HOME}/mysql-ae"
NDB_HOME=/opt/mysql-ndb
LITE_BUILD="${SCRIPT_DIR}/../../lite-version/build"
RUNTIME=/tmp/litelib-ae-mysql

kill_pid_file() {
  local file=$1 pid
  [ -f "${file}" ] || return 0
  pid=$(cat "${file}")
  kill "${pid}" 2>/dev/null || true
  for _ in $(seq 1 100); do
    kill -0 "${pid}" 2>/dev/null || return 0
    sleep 0.1
  done
  kill -9 "${pid}" 2>/dev/null || true
}

cleanup() {
  if [ -d "${RUNTIME}" ]; then
    while IFS= read -r file; do
      kill_pid_file "${file}"
    done < <(find "${RUNTIME}" -type f -name '*.pid' -print)
  fi
  for name in mysqld LiteMySQL ndbd ndb_mgmd; do
    for pid in $(pgrep -x "${name}" 2>/dev/null || true); do
      kill "${pid}" 2>/dev/null || true
    done
  done
  for pid in $(pgrep -f "${SCRIPT_DIR}/ae_overhead_monitor.py" 2>/dev/null || true); do
    kill "${pid}" 2>/dev/null || true
  done
  sudo -n systemctl stop proxysql orchestrator 2>/dev/null || true
  sudo -n rm -rf -- "${RUNTIME}" /tmp/mysql.sock /tmp/mysql.sock.lock \
    /tmp/lite_mysql /tmp/mysql_full_to_lite /tmp/mysql_lite_to_full
}

start_process() {
  local dir=$1 pidfile=$2 logfile=$3
  shift 3
  mkdir -p "${dir}"
  nohup "$@" >"${logfile}" 2>&1 </dev/null &
  echo "$!" >"${pidfile}"
}

wait_mysql() {
  local socket=$1
  for _ in $(seq 1 600); do
    "${MYSQL_HOME}/bin/mysqladmin" --socket="${socket}" -uroot ping \
      >/dev/null 2>&1 && return
    sleep 0.2
  done
  echo "MySQL did not become ready on ${socket}" >&2
  return 1
}

start_classic() {
  local prefix=$1 role=$2 server_id=$3 port=$4 socket=$5
  local report_host=${6:-}
  local flush_mode=${7:-1}
  local sync_binlog_mode=${8:-1}
  local binlog_format=${9:-ROW}
  local dir="${RUNTIME}/${prefix}/classic"
  mkdir -p "${dir}/data" "$(dirname "${socket}")"
  cat >"${dir}/my.cnf" <<EOF
[mysqld]
basedir=${MYSQL_HOME}
datadir=${dir}/data
port=${port}
socket=${socket}
pid-file=${dir}/mysqld.pidfile
bind-address=0.0.0.0
skip-name-resolve
query_cache_type=ON
server_id=${server_id}
innodb_flush_log_at_trx_commit=${flush_mode}
sync_binlog=${sync_binlog_mode}
EOF
  if [ -n "${report_host}" ]; then
    cat >>"${dir}/my.cnf" <<EOF
report_host=${report_host}
report_port=${port}
EOF
  fi
  if [ "${role}" != standalone ]; then
    cat >>"${dir}/my.cnf" <<EOF
log_bin=mysql-bin
binlog_format=${binlog_format}
gtid_mode=ON
enforce_gtid_consistency=ON
log_slave_updates=ON
relay_log_recovery=ON
master_info_repository=TABLE
relay_log_info_repository=TABLE
plugin_load_add=semisync_master.so
plugin_load_add=semisync_slave.so
EOF
  fi
  if [ "${role}" = replica ]; then
    cat >>"${dir}/my.cnf" <<EOF
read_only=ON
super_read_only=ON
EOF
  fi
  "${MYSQL_HOME}/bin/mysqld" --defaults-file="${dir}/my.cnf" \
    --initialize-insecure
  start_process "${dir}" "${dir}/mysqld.pid" "${dir}/mysqld.log" \
    "${MYSQL_HOME}/bin/mysqld" --defaults-file="${dir}/my.cnf"
  wait_mysql "${socket}"
}

setup_primary() {
  local prefix=$1 socket=$2
  "${MYSQL_HOME}/bin/mysql" --socket="${socket}" -uroot <<'SQL'
CREATE DATABASE sbtest;
CREATE USER 'sbtest'@'%' IDENTIFIED BY 'password';
GRANT ALL PRIVILEGES ON sbtest.* TO 'sbtest'@'%';
CREATE USER 'repl'@'%' IDENTIFIED BY 'STRONG_PASS';
GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%';
CREATE USER 'monitor'@'%' IDENTIFIED BY 'MONITOR_PASS';
GRANT USAGE, REPLICATION CLIENT ON *.* TO 'monitor'@'%';
CREATE USER 'orchestrator'@'%' IDENTIFIED BY 'ORC_PASS';
GRANT SUPER, RELOAD, PROCESS, REPLICATION SLAVE, REPLICATION CLIENT ON *.* TO 'orchestrator'@'%';
FLUSH PRIVILEGES;
SQL
}

setup_replica() {
  local prefix=$1 socket=$2
  "${MYSQL_HOME}/bin/mysql" --socket="${socket}" -uroot <<'SQL'
CHANGE MASTER TO MASTER_HOST='node2', MASTER_PORT=60000,
  MASTER_USER='repl', MASTER_PASSWORD='STRONG_PASS',
  MASTER_AUTO_POSITION=1;
START SLAVE;
SET GLOBAL rpl_semi_sync_slave_enabled=ON;
SQL
}

enable_semisync_primary() {
  local socket=$1
  "${MYSQL_HOME}/bin/mysql" --socket="${socket}" -uroot <<'SQL'
SET GLOBAL rpl_semi_sync_master_enabled=ON;
SET GLOBAL rpl_semi_sync_master_timeout=1000;
SQL
}

check_semisync_primary() {
  local socket=$1
  "${MYSQL_HOME}/bin/mysql" --socket="${socket}" -uroot -Nse \
    "SHOW STATUS LIKE 'Rpl_semi_sync_master_status'" |
    grep -q $'\tON$'
}

start_lite() {
  local prefix=$1
  local dir="${RUNTIME}/${prefix}/lite"
  start_process "${dir}" "${dir}/lite.pid" "${dir}/lite.log" \
    env GLOG_stderrthreshold=0 GLOG_logtostderr=1 \
    "${LITE_BUILD}/LiteMySQL"
  for _ in $(seq 1 300); do
    (echo >/dev/tcp/127.0.0.1/59999) >/dev/null 2>&1 && return
    sleep 0.1
  done
  echo "LiteMySQL did not become ready" >&2
  return 1
}

start_monitor() {
  local duration=$1 prefix=$2
  local dir="${RUNTIME}/${prefix}/monitor"
  start_process "${dir}" "${dir}/monitor.pid" "${dir}/monitor.stdout" \
    python3 -u "${SCRIPT_DIR}/ae_overhead_monitor.py" \
    "${duration}" "${dir}/monitor.jsonl"
}

wait_monitor() {
  local prefix=$1
  local file="${RUNTIME}/${prefix}/monitor/monitor.pid"
  [ -f "${file}" ] || return 0
  local pid
  pid=$(cat "${file}")
  for _ in $(seq 1 1200); do
    kill -0 "${pid}" 2>/dev/null || return 0
    sleep 0.1
  done
  return 1
}

start_ndb_mgmd() {
  local prefix=$1
  local dir="${RUNTIME}/${prefix}/ndb-mgmd"
  mkdir -p "${dir}/data"
  cat >"${dir}/config.ini" <<EOF
[NDBD DEFAULT]
NoOfReplicas=2
DataMemory=1024M
IndexMemory=256M
[TCP DEFAULT]
[NDB_MGMD]
NodeId=1
HostName=node0
DataDir=${dir}/data
[NDBD]
NodeId=2
HostName=node2
DataDir=/tmp/litelib-ae-mysql/${prefix}/ndbd/data
[NDBD]
NodeId=3
HostName=node3
DataDir=/tmp/litelib-ae-mysql/${prefix}/ndbd/data
[MYSQLD]
NodeId=50
HostName=node2
[MYSQLD]
NodeId=51
HostName=node3
EOF
  start_process "${dir}" "${dir}/mgmd.pid" "${dir}/mgmd.log" \
    "${NDB_HOME}/bin/ndb_mgmd" --nodaemon \
    --config-file="${dir}/config.ini" --configdir="${dir}/data" \
    --ndb-nodeid=1 --initial
}

start_ndbd() {
  local prefix=$1 node_id=$2
  local dir="${RUNTIME}/${prefix}/ndbd"
  mkdir -p "${dir}/data"
  start_process "${dir}" "${dir}/ndbd.pid" "${dir}/ndbd.log" \
    "${NDB_HOME}/bin/ndbd" --nodaemon --initial \
    --ndb-nodeid="${node_id}" --connect-string=node0:1186
}

start_ndb_sql() {
  local prefix=$1 node_id=$2
  local dir="${RUNTIME}/${prefix}/ndb-sql"
  mkdir -p "${dir}/data"
  cat >"${dir}/my.cnf" <<EOF
[mysqld]
basedir=${NDB_HOME}
datadir=${dir}/data
port=50000
socket=${dir}/mysql.sock
pid-file=${dir}/mysqld.pidfile
bind-address=0.0.0.0
query_cache_type=ON
ndbcluster
ndb-connectstring=node0:1186
skip-name-resolve
EOF
  "${NDB_HOME}/bin/mysqld" --defaults-file="${dir}/my.cnf" \
    --initialize-insecure
  start_process "${dir}" "${dir}/mysqld.pid" "${dir}/mysqld.log" \
    "${NDB_HOME}/bin/mysqld" --defaults-file="${dir}/my.cnf"
  for _ in $(seq 1 600); do
    "${NDB_HOME}/bin/mysqladmin" --socket="${dir}/mysql.sock" -uroot ping \
      >/dev/null 2>&1 && return
    sleep 0.2
  done
  return 1
}

setup_ndb_sql() {
  local prefix=$1 create_db=$2
  local socket="${RUNTIME}/${prefix}/ndb-sql/mysql.sock"
  if [ "${create_db}" = yes ]; then
    "${NDB_HOME}/bin/mysql" --socket="${socket}" -uroot \
      -e "CREATE DATABASE sbtest;"
  fi
  "${NDB_HOME}/bin/mysql" --socket="${socket}" -uroot <<'SQL'
CREATE USER 'sbtest'@'%' IDENTIFIED BY 'password';
GRANT ALL PRIVILEGES ON sbtest.* TO 'sbtest'@'%';
CREATE USER 'monitor'@'%' IDENTIFIED BY 'MONITOR_PASS';
GRANT USAGE ON *.* TO 'monitor'@'%';
FLUSH PRIVILEGES;
SQL
}

case "${1:-}" in
cleanup) cleanup ;;
start-classic) start_classic "$2" "$3" "$4" "$5" "$6" "${7:-}" ;;
start-classic-overhead) start_classic "$2" "$3" "$4" "$5" "$6" "" 0 0 STATEMENT ;;
setup-primary) setup_primary "$2" "$3" ;;
setup-replica) setup_replica "$2" "$3" ;;
enable-semisync-primary) enable_semisync_primary "$3" ;;
check-semisync-primary) check_semisync_primary "$3" ;;
start-lite) start_lite "$2" ;;
start-monitor) start_monitor "$2" "$3" ;;
wait-monitor) wait_monitor "$2" ;;
start-ndb-mgmd) start_ndb_mgmd "$2" ;;
start-ndbd) start_ndbd "$2" "$3" ;;
start-ndb-sql) start_ndb_sql "$2" "$3" ;;
setup-ndb-sql) setup_ndb_sql "$2" "$3" ;;
*) exit 2 ;;
esac
