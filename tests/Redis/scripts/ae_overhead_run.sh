#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/redis-overhead/$(date +%Y%m%d-%H%M%S)}
REPEATS=${AE_REPEATS:-3}
MONITOR_SECONDS=${AE_MONITOR_SECONDS:-180}
RECORD_COUNT=${AE_RECORD_COUNT:-1000000}
OPERATION_COUNT=${AE_OPERATION_COUNT:-5000000}
TARGET=${AE_TARGET:-30000}
MODES=${AE_MODES:-"vanilla embedded replica"}
WORKLOAD="${REMOTE}/tests/Redis/scripts/config/ycsb_workload"
YCSB="${HOME}/YCSB-redis-ae"
mkdir -p "${OUT}"

node_cmd() {
  local node=$1
  shift
  ssh "${node}" "'${REMOTE}/tests/Redis/scripts/ae_overhead_node.sh' $*"
}

cleanup_nodes() {
  for node in node1 node2 node3; do
    node_cmd "${node}" cleanup || true
  done
}
trap cleanup_nodes EXIT

copy_log() {
  local node=$1 prefix=$2 remote_name=$3 local_name=$4
  scp -q \
    "${node}:/tmp/litelib-ae-redis/${prefix}/${remote_name}" \
    "${OUT}/${local_name}"
}

wait_replica() {
  for _ in $(seq 1 300); do
    if ssh node1 "redis-cli -h 127.0.0.1 -p 16379 INFO replication" |
        tr -d '\r' | grep -q '^master_link_status:up$'; then
      return
    fi
    sleep 0.2
  done
  echo "Redis replica did not synchronize" >&2
  return 1
}

start_mode() {
  local mode=$1 prefix=$2
  cleanup_nodes
  node_cmd node3 start-master "${mode}" "${prefix}"
  if [ "${mode}" = replica ]; then
    node_cmd node1 start-replica "${prefix}"
    wait_replica
    for node in node1 node2 node3; do
      node_cmd "${node}" start-sentinel "${prefix}"
    done
  fi
  if [ "${mode}" = replica ]; then
    for node in node1 node2 node3; do
      node_cmd "${node}" start-monitor "${MONITOR_SECONDS}" "${prefix}"
    done
  else
    node_cmd node3 start-monitor "${MONITOR_SECONDS}" "${prefix}"
  fi
}

run_ycsb() {
  local mode=$1 prefix=$2
  local log="${OUT}/benchmark-${prefix}.log"
  local connection
  if [ "${mode}" = replica ]; then
    connection="-p redis.sentinel=10.10.1.3:26379 -p redis.sentinel.master=vanilla_redis"
  else
    connection="-p redis.host=10.10.1.4 -p redis.port=16379"
  fi

  ssh node2 "cd '${YCSB}' &&
    ./bin/ycsb load redis -s -P '${WORKLOAD}' ${connection} \
      -p recordcount='${RECORD_COUNT}' -p operationcount='${OPERATION_COUNT}' \
      -p target='${TARGET}'" \
    >"${log}" 2>&1
  sleep 20
  ssh node2 "cd '${YCSB}' &&
    ./bin/ycsb run redis -s -P '${WORKLOAD}' ${connection} \
      -p recordcount='${RECORD_COUNT}' -p operationcount='${OPERATION_COUNT}' \
      -p target='${TARGET}'" \
    >>"${log}" 2>&1
}

collect_mode() {
  local mode=$1 prefix=$2
  node_cmd node3 wait-monitor "${prefix}"
  if [ "${mode}" = replica ]; then
    node_cmd node1 wait-monitor "${prefix}"
    node_cmd node2 wait-monitor "${prefix}"
  fi
  copy_log node3 "${prefix}" master/redis.log "redis-node3-${prefix}.log"
  copy_log node3 "${prefix}" monitor/monitor.jsonl "monitor-node3-${prefix}.log"
  if [ "${mode}" = embedded ]; then
    copy_log node3 "${prefix}" master/lite.log "lite-node3-${prefix}.log"
  elif [ "${mode}" = replica ]; then
    copy_log node1 "${prefix}" replica/redis.log "redis-node1-${prefix}.log"
    for node in node1 node2 node3; do
      copy_log "${node}" "${prefix}" sentinel/sentinel.log \
        "sentinel-${node}-${prefix}.log"
    done
    copy_log node1 "${prefix}" monitor/monitor.jsonl \
      "monitor-node1-${prefix}.log"
    copy_log node2 "${prefix}" monitor/monitor.jsonl \
      "monitor-node2-${prefix}.log"
  fi
}

for mode in ${MODES}; do
  for rep in $(seq 1 "${REPEATS}"); do
    prefix="${mode}-${rep}"
    echo "==> ${mode} ${rep}/${REPEATS}"
    start_mode "${mode}" "${prefix}"
    run_ycsb "${mode}" "${prefix}"
    collect_mode "${mode}" "${prefix}"
  done
done

echo "  [ OK ] Redis raw output -> ${OUT}"
