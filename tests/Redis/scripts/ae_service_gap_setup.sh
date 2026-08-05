#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
JOBS=${AE_JOBS:-32}

ssh node3 "mkdir -p '${REMOTE}/src' '${REMOTE}/tests/Redis'"
rsync -az --delete --exclude .git --exclude build/ \
  -e ssh "${ROOT}/src/" "node3:${REMOTE}/src/"
rsync -az --delete --exclude .git --exclude build/ \
  -e ssh "${ROOT}/tests/Redis/" "node3:${REMOTE}/tests/Redis/"
ssh node3 "sudo -n apt-get update -qq &&
  sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    build-essential cmake libevent-dev libgoogle-glog-dev lsof redis-tools &&
  cd '${REMOTE}/tests/Redis/src/lite-version' &&
  rm -rf build &&
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release &&
  cmake --build build -j'${JOBS}' &&
  test -x build/redis-lite &&
  test -x build/Lite/lite_cli"
echo "  [ OK ] Redis proxy service-gap environment ready"
