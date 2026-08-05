#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REDIS_DIR="${SCRIPT_DIR}/../src/redis"
LITE_BUILD="${SCRIPT_DIR}/../src/lite-version/build"
RUNTIME_ROOT=/tmp/litelib-ae-redis

kill_pid_file() {
  local file=$1 pid
  [ -f "${file}" ] || return
  pid=$(cat "${file}")
  if kill -0 "${pid}" 2>/dev/null; then
    kill "${pid}" 2>/dev/null || true
    for _ in $(seq 1 50); do
      kill -0 "${pid}" 2>/dev/null || break
      sleep 0.1
    done
    kill -9 "${pid}" 2>/dev/null || true
  fi
}

cleanup() {
  if [ -d "${RUNTIME_ROOT}" ]; then
    while IFS= read -r file; do
      kill_pid_file "${file}"
    done < <(find "${RUNTIME_ROOT}" -type f -name '*.pid' -print)
  fi
  for name in redis-server redis-server-vanilla redis-sentinel-vanilla redis-lite; do
    for pid in $(pgrep -x "${name}" 2>/dev/null || true); do
      kill "${pid}" 2>/dev/null || true
    done
  done
  rm -f /tmp/redis.sock /tmp/lite_Redis /dev/shm/lite_shared_memory
  rm -rf "${RUNTIME_ROOT}"
}

wait_redis() {
  local port=$1
  for _ in $(seq 1 300); do
    redis-cli -h 127.0.0.1 -p "${port}" ping 2>/dev/null |
      grep -q PONG && return
    sleep 0.1
  done
  echo "Redis did not become ready on port ${port}" >&2
  return 1
}

start_process() {
  local dir=$1 pidfile=$2 logfile=$3
  shift 3
  mkdir -p "${dir}"
  nohup "$@" >"${logfile}" 2>&1 </dev/null &
  echo "$!" >"${pidfile}"
}

start_master() {
  local mode=$1 prefix=$2
  local dir="${RUNTIME_ROOT}/${prefix}/master"
  local binary="${REDIS_DIR}/src/redis-server-vanilla"
  local source_config="${SCRIPT_DIR}/config/vanilla.conf"
  mkdir -p "${dir}/data"
  if [ "${mode}" = embedded ]; then
    binary="${REDIS_DIR}/src/redis-server"
    source_config="${SCRIPT_DIR}/config/embedded.conf"
  fi
  cp "${source_config}" "${dir}/redis.conf"
  printf '\ndir "%s"\ndbfilename dump.rdb\npidfile "%s"\n' \
    "${dir}/data" "${dir}/redis.pidfile" >>"${dir}/redis.conf"
  export LD_LIBRARY_PATH="${LITE_BUILD}:${LD_LIBRARY_PATH:-}"
  start_process "${dir}" "${dir}/redis.pid" "${dir}/redis.log" \
    taskset -c 36,37,38,39 "${binary}" "${dir}/redis.conf"
  wait_redis 16379

  if [ "${mode}" = embedded ]; then
    start_process "${dir}" "${dir}/lite.pid" "${dir}/lite.log" \
      taskset -c 30,31,32,33,34,35 "${LITE_BUILD}/redis-lite"
    for _ in $(seq 1 300); do
      [ -S /tmp/lite_Redis ] && return
      sleep 0.1
    done
    echo "Redis Lite did not create /tmp/lite_Redis" >&2
    return 1
  fi
}

start_replica() {
  local prefix=$1
  local dir="${RUNTIME_ROOT}/${prefix}/replica"
  mkdir -p "${dir}/data"
  cp "${SCRIPT_DIR}/config/replica.conf" "${dir}/redis.conf"
  printf '\ndir "%s"\ndbfilename dump.rdb\npidfile "%s"\n' \
    "${dir}/data" "${dir}/redis.pidfile" >>"${dir}/redis.conf"
  start_process "${dir}" "${dir}/redis.pid" "${dir}/redis.log" \
    taskset -c 36,37,38,39 \
    "${REDIS_DIR}/src/redis-server-vanilla" "${dir}/redis.conf"
  wait_redis 16379
}

start_sentinel() {
  local prefix=$1
  local dir="${RUNTIME_ROOT}/${prefix}/sentinel"
  mkdir -p "${dir}"
  cp "${SCRIPT_DIR}/config/sentinel.conf" "${dir}/sentinel.conf"
  printf '\ndir "%s"\npidfile "%s"\n' \
    "${dir}" "${dir}/sentinel.pidfile" >>"${dir}/sentinel.conf"
  start_process "${dir}" "${dir}/sentinel.pid" "${dir}/sentinel.log" \
    taskset -c 28,29 \
    "${REDIS_DIR}/src/redis-sentinel-vanilla" "${dir}/sentinel.conf"
  for _ in $(seq 1 300); do
    redis-cli -h 127.0.0.1 -p 26379 ping 2>/dev/null |
      grep -q PONG && return
    sleep 0.1
  done
  echo "Redis Sentinel did not become ready" >&2
  return 1
}

start_monitor() {
  local duration=$1 prefix=$2
  local dir="${RUNTIME_ROOT}/${prefix}/monitor"
  mkdir -p "${dir}"
  start_process "${dir}" "${dir}/monitor.pid" "${dir}/monitor.stdout" \
    python3 -u "${SCRIPT_DIR}/monitor/monitor.py" \
    "${duration}" "${dir}/monitor.jsonl" 0
}

wait_monitor() {
  local prefix=$1
  local pidfile="${RUNTIME_ROOT}/${prefix}/monitor/monitor.pid"
  [ -f "${pidfile}" ] || return 0
  local pid
  pid=$(cat "${pidfile}")
  for _ in $(seq 1 2400); do
    kill -0 "${pid}" 2>/dev/null || return 0
    sleep 0.1
  done
  echo "Redis monitor did not finish" >&2
  return 1
}

case "${1:-}" in
cleanup) cleanup ;;
start-master) start_master "$2" "$3" ;;
start-replica) start_replica "$2" ;;
start-sentinel) start_sentinel "$2" ;;
start-monitor) start_monitor "$2" "$3" ;;
wait-monitor) wait_monitor "$2" ;;
*)
  echo "usage: $0 {cleanup|start-master MODE PREFIX|start-replica PREFIX|start-sentinel PREFIX|start-monitor SECONDS PREFIX|wait-monitor PREFIX}" >&2
  exit 2
  ;;
esac
