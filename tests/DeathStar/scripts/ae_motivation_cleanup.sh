#!/bin/bash
# Stop all DeathStar runtime state while preserving builds and collected results.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEATHSTAR_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "==> Removing DeathStar stack and manual containers"
ssh node1 "cd '${DEATHSTAR_DIR}/scripts' &&
  if docker info >/dev/null 2>&1; then
    ./swarm_helper_replica.sh down
  else
    sudo -n ./swarm_helper_replica.sh down
  fi" >/dev/null 2>&1 || true

ssh node0 '
  d() {
    if docker info >/dev/null 2>&1; then docker "$@"; else sudo -n docker "$@"; fi
  }
  d rm -f post-storage-mongodb >/dev/null 2>&1 || true
  ! d inspect post-storage-mongodb >/dev/null 2>&1
'
ssh node3 '
  d() {
    if docker info >/dev/null 2>&1; then docker "$@"; else sudo -n docker "$@"; fi
  }
  for name in post-storage-memcached-1 post-storage-memcached-2; do
    d rm -f "$name" >/dev/null 2>&1 || true
    ! d inspect "$name" >/dev/null 2>&1
  done
'
ssh node1 '
  d() {
    if docker info >/dev/null 2>&1; then docker "$@"; else sudo -n docker "$@"; fi
  }
  ! d stack ls --format "{{.Name}}" | grep -qx socialnetwork
'

echo "==> Removing transient component logs/sockets"
for node in node0 node1 node2 node3; do
  ssh "$node" "
    rm -rf '${DEATHSTAR_DIR}/src/socialNetwork/docker/lite-memcached/logs'
    rm -f /tmp/memcached.sock /tmp/lite_memcached
  " || true
done

echo "  [ OK ] DeathStar runtime cleaned; builds and ~/LiteLib/results are preserved"
