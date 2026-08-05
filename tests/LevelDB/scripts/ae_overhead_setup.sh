#!/bin/bash
# Pinned LevelDB overhead setup (commit 680892c8...).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
JOBS=${AE_JOBS:-32}

echo "==> Fetching redis-leveldb source"
if [ ! -f "${ROOT}/tests/LevelDB/src/tests/redis-leveldb/Makefile" ]; then
  rm -rf "${ROOT}/tests/LevelDB/src/tests/redis-leveldb"
  git clone -q https://github.com/KDr2/redis-leveldb.git \
    "${ROOT}/tests/LevelDB/src/tests/redis-leveldb"
  git -C "${ROOT}/tests/LevelDB/src/tests/redis-leveldb" checkout -q \
    403cc6eee547a13a4b49b79b30d52bedccc04c84
fi
git -C "${ROOT}/tests/LevelDB/src/tests/redis-leveldb" \
  submodule update --init --recursive

echo "==> Syncing pinned source to node0/node1"
for node in node0 node1; do
  [ "$node" = "$(hostname -s)" ] && continue
  ssh "$node" "mkdir -p '${REMOTE}'"
  rsync -az --delete --exclude .git --exclude build/ --exclude tmp-data/ \
    -e ssh "${ROOT}/" "${node}:${REMOTE}/"
done

echo "==> Building server variants on node0"
ssh node0 bash -s -- "${REMOTE}" "${JOBS}" <<'REMOTE_SCRIPT'
set -euo pipefail
ROOT=$1
JOBS=$2
sudo -n apt-get update -qq
sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
  build-essential cmake rust-all libsnappy-dev libev-dev libgmp-dev \
  cgroup-tools python3-pip redis-tools
python3 -m pip install --user -q psutil redis numpy

cd "${ROOT}/tests/LevelDB/src/lite-version"
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"${JOBS}"

R="${ROOT}/tests/LevelDB/src/tests/redis-leveldb"
cd "${R}"
make clean >/dev/null 2>&1 || true
make -j"${JOBS}"
cp redis-leveldb redis-leveldb-vanilla
git apply --check ../scripts/leveldb/redis-leveldb.patch &&
  git apply ../scripts/leveldb/redis-leveldb.patch || true
make clean >/dev/null 2>&1 || true
make -j"${JOBS}"
test -x redis-leveldb -a -x redis-leveldb-vanilla
REMOTE_SCRIPT

echo "==> Building Rust client on node1"
ssh node1 "sudo -n apt-get update -qq &&
  sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq rust-all &&
  cd '${REMOTE}/tests/LevelDB/src/tests/client' &&
  cargo build --release"

echo "  [ OK ] LevelDB overhead environment ready"
