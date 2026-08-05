#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/table2/redis-proxy-$(date +%Y%m%d-%H%M%S)}
REPEATS=${AE_REPEATS:-3}
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

for rep in $(seq 1 "${REPEATS}"); do
  echo "==> Redis proxy ${rep}/${REPEATS}"
  cleanup
  ssh node3 bash -s -- "${REMOTE}" "${rep}" "${REDIS_SERVER}" <<'REMOTE_SCRIPT'
set -euo pipefail
REMOTE=$1
rep=$2
REDIS_SERVER=$3
dir="${REMOTE}/tests/Redis/scripts/ae-gap-${rep}"
mkdir -p "${dir}"
cp "${REMOTE}/tests/Redis/scripts/config/vanilla.conf" "${dir}/redis.conf"
printf '\ndaemonize no\nbind 10.10.1.4 127.0.0.1\nport 16379\nunixsocket /tmp/redis.sock\ndir %s\ndbfilename dump.rdb\n' \
  "${dir}" >>"${dir}/redis.conf"
nohup "${REDIS_SERVER}" "${dir}/redis.conf" >"${dir}/redis.log" 2>&1 </dev/null &
echo "$!" >"${dir}/redis.pid"
REMOTE_SCRIPT
  for _ in $(seq 1 300); do
    ssh node3 "redis-cli -h 127.0.0.1 -p 16379 ping" 2>/dev/null |
      grep -q PONG && break
    sleep 0.1
  done
  ssh node3 "nohup '${LITE}' >'${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log' 2>&1 </dev/null &
    echo \$! >'${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.pid'"
  for _ in $(seq 1 300); do
    ssh node3 "test -p /tmp/lite_Redis -o -S /tmp/lite_Redis" && break
    sleep 0.1
  done
  ssh node3 "redis-cli -h 127.0.0.1 -p 16379 SET ae-probe value >/dev/null"
  ssh node3 "'${CLI}' -t /tmp/lite_Redis -p /tmp/redis.sock -m 1"
  for _ in $(seq 1 300); do
    ssh node3 "grep -q 'Entered emergency mode' \
      '${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log'" && break
    sleep 0.1
  done
  scp -q "node3:${REMOTE}/tests/Redis/scripts/ae-gap-${rep}/lite.log" \
    "${OUT}/proxy-${rep}.log"
done

python3 "${SCRIPT_DIR}/ae_service_gap_collect.py" "${OUT}" \
  --output "${OUT}/redis-proxy.csv"
echo "  [ OK ] Redis proxy Table 2 results -> ${OUT}/redis-proxy.csv"
