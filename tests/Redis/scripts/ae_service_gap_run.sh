#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/table2/redis-proxy-$(date +%Y%m%d-%H%M%S)}
REPEATS=${AE_REPEATS:-3}
CASE_ATTEMPTS=${AE_CASE_ATTEMPTS:-4}
CASE_TIMEOUT_SECONDS=${AE_CASE_TIMEOUT_SECONDS:-300}
REDIS_CONF=${REMOTE}/tests/Redis/scripts/config/vanilla.conf
LITE=${REMOTE}/tests/Redis/src/lite-version/build/redis-lite
CLI=${REMOTE}/tests/Redis/src/lite-version/build/Lite/lite_cli
REDIS_WORKTREE=${LITELIB_REDIS_WORKTREE_DIR:-${HOME}/LiteLib-redis-overhead}
REDIS_SERVER=${AE_REDIS_SERVER:-${REDIS_WORKTREE}/tests/Redis/src/redis/src/redis-server-vanilla}
mkdir -p "${OUT}"

cleanup() {
  ssh node3 '
    for pid in $(pgrep -x redis-lite 2>/dev/null || true); do kill "$pid" 2>/dev/null || true; done
    for pid in $(lsof -t -iTCP:16379 2>/dev/null || true); do kill "$pid" 2>/dev/null || true; done
    rm -f /tmp/redis.sock /tmp/lite_Redis /dev/shm/lite_shared_memory
  ' || true
}
trap cleanup EXIT

run_proxy() {
  local rep=$1
  local ready=0
  cleanup
  ssh node3 bash -s -- "${REMOTE}" "${rep}" "${REDIS_SERVER}" <<'REMOTE_SCRIPT'
set -euo pipefail
REMOTE=$1
rep=$2
REDIS_SERVER=$3
dir="${REMOTE}/tests/Redis/scripts/ae-gap-${rep}"
rm -rf "${dir}"
mkdir -p "${dir}"
cp "${REMOTE}/tests/Redis/scripts/config/vanilla.conf" "${dir}/redis.conf"
printf '\ndaemonize no\nbind 10.10.1.4 127.0.0.1\nport 16379\nunixsocket /tmp/redis.sock\ndir %s\ndbfilename dump.rdb\n' \
  "${dir}" >>"${dir}/redis.conf"
nohup "${REDIS_SERVER}" "${dir}/redis.conf" >"${dir}/redis.log" 2>&1 </dev/null &
echo "$!" >"${dir}/redis.pid"
REMOTE_SCRIPT
  for _ in $(seq 1 300); do
    if ssh node3 "redis-cli -h 127.0.0.1 -p 16379 ping" 2>/dev/null |
        grep -q PONG; then
      ready=1
      break
    fi
    sleep 0.1
  done
  [ "${ready}" -eq 1 ] || {
    echo "Redis proxy backend did not become ready" >&2
    return 1
  }
  ssh node3 "nohup '${LITE}' >'${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log' 2>&1 </dev/null &
    echo \$! >'${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.pid'"
  ready=0
  for _ in $(seq 1 300); do
    if ssh node3 "test -p /tmp/lite_Redis -o -S /tmp/lite_Redis" &&
        ssh node3 "grep -q 'Daemon listening on /tmp/lite_Redis' \
          '${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log'"; then
      ready=1
      break
    fi
    sleep 0.1
  done
  [ "${ready}" -eq 1 ] || {
    echo "Redis proxy control pipe did not become ready" >&2
    return 1
  }
  ssh node3 "redis-cli -h 127.0.0.1 -p 16379 SET ae-probe value >/dev/null"
  timeout --signal=TERM --kill-after=5s 30s \
    ssh node3 "'${CLI}' -t /tmp/lite_Redis -p /tmp/redis.sock -m 1"
  ready=0
  for _ in $(seq 1 300); do
    if ssh node3 "grep -q 'Received message kEnterEmergencyMode' \
          '${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log' &&
        grep -q 'Entered emergency mode' \
          '${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log'"; then
      ready=1
      break
    fi
    sleep 0.1
  done
  [ "${ready}" -eq 1 ] || {
    echo "Redis proxy did not enter emergency mode" >&2
    return 1
  }
  scp -q "node3:${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log" \
    "${OUT}/proxy-${rep}.log"
}

run_case() {
  local rep=$1
  local attempt rc
  for attempt in $(seq 1 "${CASE_ATTEMPTS}"); do
    if timeout --signal=TERM --kill-after=15s "${CASE_TIMEOUT_SECONDS}s" \
        "$0" --case "${rep}"; then
      return
    else
      rc=$?
    fi
    scp -q \
      "node3:${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log" \
      "${OUT}/proxy-${rep}-attempt${attempt}.log" 2>/dev/null || true
    echo "  [WARN] Redis proxy ${rep} attempt ${attempt}/${CASE_ATTEMPTS} failed (exit ${rc})" >&2
    [ "${attempt}" -lt "${CASE_ATTEMPTS}" ] || return "${rc}"
    cleanup
    sleep 2
  done
}

if [ "${1:-}" = "--case" ]; then
  trap - EXIT
  run_proxy "$2"
  exit
fi

for rep in $(seq 1 "${REPEATS}"); do
  echo "==> Redis proxy ${rep}/${REPEATS}"
  run_case "${rep}"
done

python3 "${SCRIPT_DIR}/ae_service_gap_collect.py" "${OUT}" \
  --output "${OUT}/redis-proxy.csv"
echo "  [ OK ] Redis proxy Table 2 results -> ${OUT}/redis-proxy.csv"
