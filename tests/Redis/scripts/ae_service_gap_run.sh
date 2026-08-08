#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/table2/redis-$(date +%Y%m%d-%H%M%S)}
REPEATS=${AE_REPEATS:-3}
CRASH_AFTER=${AE_CRASH_AFTER:-30}
RECORD_COUNT=${AE_RECORD_COUNT:-1000000}
OPERATION_COUNT=${AE_OPERATION_COUNT:-3000000}
TARGET=${AE_TARGET:-30000}
MODES=${AE_MODES:-"ap-30s ap-5s embedded"}
YCSB_TIMEOUT_SECONDS=${AE_YCSB_TIMEOUT_SECONDS:-600}
CRASH_STEP_TIMEOUT_SECONDS=${AE_CRASH_STEP_TIMEOUT_SECONDS:-300}
CASE_ATTEMPTS=${AE_CASE_ATTEMPTS:-2}
YCSB="${HOME}/YCSB-redis-ae"
WORKLOAD="${REMOTE}/tests/Redis/scripts/config/ycsb_workload"
mkdir -p "${OUT}"
[ "${RECORD_COUNT}" -eq 1000000 ] || {
  echo "[FAIL] Table 2 Redis runs require the full 1,000,000-record dataset" >&2
  exit 2
}

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

run_ycsb() {
  local action=$1 log=$2 connection=$3
  ssh node2 "cd '${YCSB}' &&
    timeout --signal=TERM --kill-after=30s '${YCSB_TIMEOUT_SECONDS}s' \
      ./bin/ycsb '${action}' redis -s -P '${WORKLOAD}' ${connection} \
      -p recordcount='${RECORD_COUNT}' \
      -p operationcount='${OPERATION_COUNT}' \
      -p target='${TARGET}'" >>"${log}" 2>&1
}

run_case() {
  local label=$1
  shift
  local attempt rc
  for attempt in $(seq 1 "${CASE_ATTEMPTS}"); do
    if "$@"; then
      return 0
    else
      rc=$?
    fi
    echo "  [WARN] ${label} attempt ${attempt}/${CASE_ATTEMPTS} failed (exit ${rc})" >&2
    [ "${attempt}" -lt "${CASE_ATTEMPTS}" ] || return "${rc}"
    cleanup_nodes
    sleep 10
  done
}

wait_ycsb_start() {
  local log=$1 previous=$2
  for _ in $(seq 1 600); do
    current=$(grep -c ' 0 sec: 0 operations' "${log}" 2>/dev/null || true)
    [ "${current}" -gt "${previous}" ] && return
    sleep 0.1
  done
  echo "YCSB did not start" >&2
  return 1
}

wait_replica() {
  for _ in $(seq 1 300); do
    if ssh node1 "redis-cli -h 127.0.0.1 -p 16379 INFO replication" |
        tr -d '\r' | grep -q '^master_link_status:up$'; then
      return
    fi
    sleep 0.2
  done
  return 1
}

run_ap() {
  local label=$1 threshold=$2 rep=$3
  local prefix="${label}-${rep}"
  local log="${OUT}/benchmark-${prefix}.log"
  : >"${log}"
  cleanup_nodes
  node_cmd node3 start-master vanilla "${prefix}"
  node_cmd node1 start-replica "${prefix}"
  wait_replica
  for node in node1 node2 node3; do
    node_cmd "${node}" start-sentinel "${prefix}" "${threshold}"
  done
  run_ycsb load "${log}" "-p redis.host=10.10.1.4 -p redis.port=16379"
  starts=$(grep -c ' 0 sec: 0 operations' "${log}" 2>/dev/null || true)
  run_ycsb run "${log}" \
    "-p redis.sentinel=10.10.1.3:26379 -p redis.sentinel.master=vanilla_redis" &
  bench_pid=$!
  wait_ycsb_start "${log}" "${starts}"
  sleep "${CRASH_AFTER}"
  ssh node3 "pid=\$(lsof -t -iTCP@10.10.1.4:16379 | head -1);
    kill -9 \"\${pid}\"" >"${OUT}/crash-${prefix}.log" 2>&1
  wait "${bench_pid}"
}

run_embedded() {
  local rep=$1
  local prefix="embedded-${rep}"
  local log="${OUT}/benchmark-${prefix}.log"
  : >"${log}"
  cleanup_nodes
  node_cmd node3 start-master embedded "${prefix}"
  run_ycsb load "${log}" "-p redis.host=10.10.1.4 -p redis.port=16379"
  starts=$(grep -c ' 0 sec: 0 operations' "${log}" 2>/dev/null || true)
  run_ycsb run "${log}" "-p redis.host=10.10.1.4 -p redis.port=16379" &
  bench_pid=$!
  wait_ycsb_start "${log}" "${starts}"
  sleep "${CRASH_AFTER}"
  (
    timeout --signal=TERM --kill-after=15s "${CRASH_STEP_TIMEOUT_SECONDS}s" \
      ssh node3 "pid=\$(lsof -t -iTCP@10.10.1.4:16379 | head -1);
      kill -15 \"\${pid}\";
      while kill -0 \"\${pid}\" 2>/dev/null; do sleep 0.01; done"
    timeout --signal=TERM --kill-after=15s "${CRASH_STEP_TIMEOUT_SECONDS}s" \
      ssh node3 "'${REMOTE}/tests/Redis/scripts/ae_overhead_node.sh' \
        restart-embedded-full '${prefix}'"
    ready=0
    for _ in $(seq 1 3000); do
      ssh node3 "grep -q 'Ready to accept connections' \
        '/tmp/litelib-ae-redis/${prefix}/master/redis.log'" && {
          ready=1
          break
        }
      sleep 0.1
    done
    [ "${ready}" -eq 1 ] || {
      echo "Redis did not become ready after embedded restart" >&2
      exit 1
    }
    timeout --signal=TERM --kill-after=15s "${CRASH_STEP_TIMEOUT_SECONDS}s" \
      ssh node3 "'${REMOTE}/tests/Redis/src/lite-version/build/Lite/lite_cli' \
        -t /tmp/lite_Redis -p /tmp/redis.sock -m 0"
    replayed=0
    for _ in $(seq 1 3000); do
      ssh node3 "grep -q 'Replay took' \
        '/tmp/litelib-ae-redis/${prefix}/master/lite.log'" && {
          replayed=1
          break
        }
      sleep 0.1
    done
    [ "${replayed}" -eq 1 ] || {
      echo "Redis replay did not complete" >&2
      exit 1
    }
  ) >"${OUT}/crash-${prefix}.log" 2>&1 &
  crash_pid=$!
  wait "${bench_pid}"
  wait "${crash_pid}"
  scp -q "node3:/tmp/litelib-ae-redis/${prefix}/master/lite.log" \
    "${OUT}/lite-${prefix}.log"
}

for rep in $(seq 1 "${REPEATS}"); do
  for mode in ${MODES}; do
    case "${mode}" in
    ap-30s)
      echo "==> Redis active-passive 30s ${rep}/${REPEATS}"
      run_case "Redis active-passive 30s ${rep}" \
        run_ap ap-30s 30000 "${rep}"
      ;;
    ap-5s)
      echo "==> Redis active-passive 5s ${rep}/${REPEATS}"
      run_case "Redis active-passive 5s ${rep}" \
        run_ap ap-5s 5000 "${rep}"
      ;;
    embedded)
      echo "==> Redis embedded ${rep}/${REPEATS}"
      run_case "Redis embedded ${rep}" run_embedded "${rep}"
      ;;
    esac
  done
done

python3 "${SCRIPT_DIR}/ae_service_gap_collect.py" "${OUT}" \
  --output "${OUT}/redis.csv"
echo "  [ OK ] Redis Table 2 results -> ${OUT}/redis.csv"
