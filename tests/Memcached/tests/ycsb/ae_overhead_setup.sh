#!/bin/bash
# Figures 15/16 setup for exact source commit b74d69377e96101d...
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE_DIR=${AE_REMOTE_DIR:-${WORKTREE}}
SRC="${REMOTE_DIR}/tests/Memcached/src"
JOBS=${AE_JOBS:-40}
YCSB_COMMIT=9706b6b6f370c225065f33d7cdb39e94cc5d8cf7

echo "==> Stopping DeathStar services so node3 is dedicated to YCSB"
ssh node1 'docker stack rm socialnetwork >/dev/null 2>&1 || true'
ssh node3 '
  for name in post-storage-memcached-1 post-storage-memcached-2; do
    docker stop "$name" >/dev/null 2>&1 || true
    docker rm -f "$name" >/dev/null 2>&1 || true
  done
'
sleep 15

echo "==> Syncing exact overhead commit to node2/node3"
for node in node2 node3; do
  ssh "$node" "mkdir -p '${REMOTE_DIR}'"
  rsync -az --delete \
    --exclude '.git' --exclude 'build/' --exclude 'logs/' \
    --exclude 'tests/Memcached/src/memcached/' \
    --exclude 'tests/Memcached/src/memcached-vanilla' \
    --exclude 'tests/Memcached/src/memcached-1.6.14.tar.gz' \
    -e ssh "${WORKTREE}/" "${node}:${REMOTE_DIR}/"
done

echo "==> Building vanilla, proxy and embedded Memcached variants on node3"
ssh node3 bash -s -- "${SRC}" "${JOBS}" <<'REMOTE'
set -euo pipefail
SRC=$1
JOBS=$2
ROOT=$(cd "${SRC}/../../.." && pwd)
GLOG_PREFIX="${ROOT}/.deps/glog"
sudo -n apt-get update -qq
sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
  build-essential autoconf automake libtool cmake wget git \
  libevent-dev libgoogle-glog-dev python3 python3-pip
python3 -m pip install --user -q psutil

if [ ! -f "${GLOG_PREFIX}/lib/libglog.so" ]; then
  GLOG_BUILD=$(mktemp -d --tmpdir glog-ae.XXXXXX)
  trap 'rm -rf "${GLOG_BUILD}"' EXIT
  GIT_ADVICE=0 git clone -q --branch v0.4.0 --depth 1 \
    https://github.com/google/glog.git "${GLOG_BUILD}"
  cmake -S "${GLOG_BUILD}" -B "${GLOG_BUILD}/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${GLOG_PREFIX}" \
    -DBUILD_SHARED_LIBS=ON -DWITH_GFLAGS=OFF
  cmake --build "${GLOG_BUILD}/build" -j"${JOBS}"
  cmake --install "${GLOG_BUILD}/build"
fi

cd "${SRC}/lite-version-ascii"
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${GLOG_PREFIX};/usr/local" \
  -DCMAKE_BUILD_RPATH="${GLOG_PREFIX}/lib" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath,${GLOG_PREFIX}/lib" ..
make -j"${JOBS}"

cd "${SRC}/lite-version-ascii-embedded"
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_TCMALLOC=OFF \
  -DCMAKE_PREFIX_PATH="${GLOG_PREFIX};/usr/local" \
  -DCMAKE_BUILD_RPATH="${GLOG_PREFIX}/lib" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath,${GLOG_PREFIX}/lib" \
  -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-rpath,${GLOG_PREFIX}/lib" ..
make -j"${JOBS}"

cd "${SRC}"
if [ ! -x memcached/memcached-vanilla ]; then
  [ -f memcached-1.6.14.tar.gz ] ||
    wget -q https://memcached.org/files/memcached-1.6.14.tar.gz
  rm -rf memcached
  mkdir memcached
  tar -xzf memcached-1.6.14.tar.gz --strip-components=1 -C memcached
  cd memcached
  ./configure --quiet
  make -j"${JOBS}" memcached
  cp memcached ./memcached-vanilla
  make distclean >/dev/null 2>&1 || true
fi

cd "${SRC}/memcached"
if [ ! -f .litelib-patched ]; then
  patch -p1 < "${SRC}/memcached.1.6.14.patch"
  touch .litelib-patched
fi
mkdir -p vendor/LiteSys
ln -sfn "${SRC}/lite-version-ascii-embedded/build" vendor/LiteSys/build
ln -sfn "${SRC}/lite-version-ascii-embedded/Lite/include/embedded_lite.h" \
  vendor/LiteSys/embedded_lite.h
autoreconf -i >/dev/null 2>&1 || true
CPPFLAGS="-I${GLOG_PREFIX}/include" \
LDFLAGS="-L${GLOG_PREFIX}/lib -Wl,-rpath,${GLOG_PREFIX}/lib" \
  ./configure --quiet
make -j"${JOBS}" memcached
if ldd "${SRC}/lite-version-ascii-embedded/build/libembedded_lite_memcached.so" |
   grep -q tcmalloc; then
  echo "ERROR: embedded build unexpectedly links tcmalloc" >&2
  exit 1
fi
REMOTE

echo "==> Installing pinned YCSB on node2"
ssh node2 bash -s -- "${YCSB_COMMIT}" <<'REMOTE'
set -euo pipefail
COMMIT=$1
sudo -n apt-get update -qq
sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
  git openjdk-11-jdk maven
if [ ! -d "${HOME}/YCSB/.git" ]; then
  rm -rf "${HOME}/YCSB"
  git clone -q https://github.com/brianfrankcooper/YCSB.git "${HOME}/YCSB"
fi
git -C "${HOME}/YCSB" fetch -q origin
git -C "${HOME}/YCSB" checkout -q --detach "${COMMIT}"
cd "${HOME}/YCSB"
mvn -q -pl site.ycsb:memcached-binding -am package -DskipTests
REMOTE

echo "  [ OK ] Figures 15/16 environment ready"
