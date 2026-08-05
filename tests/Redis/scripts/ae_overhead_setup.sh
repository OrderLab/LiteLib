#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
JOBS=${AE_JOBS:-32}
REDIS_COMMIT=74b289a0e12f9f65a6daeec6a66cadc76792f644
YCSB_COMMIT=33296cd1493ee48e518a8fe81f9e0d76c8562e35

echo "==> Syncing Redis experiment source"
for node in node1 node2 node3; do
  ssh "${node}" "mkdir -p '${REMOTE}/src' '${REMOTE}/tests/Redis'"
  rsync -az --delete --exclude .git --exclude build/ \
    -e ssh "${ROOT}/src/" "${node}:${REMOTE}/src/"
  rsync -az --delete --exclude .git --exclude build/ --exclude ae-runtime/ \
    -e ssh "${ROOT}/tests/Redis/" "${node}:${REMOTE}/tests/Redis/"
done

echo "==> Installing runtime dependencies"
pids=()
for node in node1 node2 node3; do
  ssh "${node}" "sudo -n apt-get update -qq &&
    sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
      build-essential cmake git lsof maven numactl pkg-config \
      python3-pip redis-tools libevent-dev libgoogle-glog-dev \
      libgoogle-perftools-dev libssl-dev libsystemd-dev &&
    sudo -n sysctl -w vm.overcommit_memory=1 >/dev/null &&
    python3 -m pip install --user -q psutil" &
  pids+=("$!")
done
for pid in "${pids[@]}"; do
  wait "${pid}"
done

echo "==> Building vanilla and embedded Redis on node3"
ssh node3 bash -s -- "${REMOTE}" "${JOBS}" "${REDIS_COMMIT}" <<'REMOTE_SCRIPT'
set -euo pipefail
ROOT=$1
JOBS=$2
REDIS_COMMIT=$3
REDIS_DIR="${ROOT}/tests/Redis/src/redis"
LITE_DIR="${ROOT}/tests/Redis/src/lite-version"

rm -rf "${REDIS_DIR}"
git clone -q https://github.com/redis/redis.git "${REDIS_DIR}"
git -C "${REDIS_DIR}" checkout -q --detach "${REDIS_COMMIT}"
make -C "${REDIS_DIR}" -j"${JOBS}" BUILD_TLS=no
cp -Lf "${REDIS_DIR}/src/redis-server" \
  "${REDIS_DIR}/src/redis-server-vanilla"
cp -Lf "${REDIS_DIR}/src/redis-sentinel" \
  "${REDIS_DIR}/src/redis-sentinel-vanilla"

cmake -S "${LITE_DIR}" -B "${LITE_DIR}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${LITE_DIR}/build" -j"${JOBS}"

git -C "${REDIS_DIR}" apply "${ROOT}/tests/Redis/src/redis.7.4.1.patch"
make -C "${REDIS_DIR}" clean
make -C "${REDIS_DIR}" -j"${JOBS}" BUILD_TLS=no

test -x "${REDIS_DIR}/src/redis-server"
test -x "${REDIS_DIR}/src/redis-server-vanilla"
test -x "${REDIS_DIR}/src/redis-sentinel-vanilla"
test -x "${LITE_DIR}/build/redis-lite"
ldd "${REDIS_DIR}/src/redis-server" | grep -q embedded_lite_redis
REMOTE_SCRIPT

echo "==> Distributing vanilla Redis binaries"
for node in node1 node2; do
  ssh "${node}" "mkdir -p '${REMOTE}/tests/Redis/src/redis/src'"
  for binary in redis-server-vanilla redis-sentinel-vanilla; do
    ssh node3 "cat '${REMOTE}/tests/Redis/src/redis/src/${binary}'" |
      ssh "${node}" "cat > '${REMOTE}/tests/Redis/src/redis/src/${binary}' &&
        chmod +x '${REMOTE}/tests/Redis/src/redis/src/${binary}'"
  done
done

echo "==> Building patched YCSB on node2"
ssh node2 bash -s -- "${REMOTE}" "${YCSB_COMMIT}" <<'REMOTE_SCRIPT'
set -euo pipefail
ROOT=$1
YCSB_COMMIT=$2
YCSB="${HOME}/YCSB-redis-ae"
rm -rf "${YCSB}"
git clone -q https://github.com/brianfrankcooper/YCSB.git "${YCSB}"
git -C "${YCSB}" checkout -q --detach "${YCSB_COMMIT}"
git -C "${YCSB}" apply \
  "${ROOT}/tests/Redis/scripts/config/YCSB_sentinel_stale.patch"
cd "${YCSB}"
mvn -q -pl site.ycsb:redis-binding -am package -DskipTests
test -x bin/ycsb
REMOTE_SCRIPT

echo "  [ OK ] Redis overhead environment ready"
