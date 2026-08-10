#!/bin/bash
# Stop all DeathStar runtime state while preserving builds and collected results.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEATHSTAR_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "==> Stopping remote experiment drivers"
ssh node3 'bash -s' <<'REMOTE_SCRIPT' || true
kill_tree() {
  local pid=$1 child
  for child in $(ps -o pid= --ppid "${pid}" 2>/dev/null); do
    kill_tree "${child}"
  done
  kill -TERM "${pid}" 2>/dev/null || true
}

for pid in $(pgrep -f '[r]un_exp_replica\.sh' 2>/dev/null || true); do
  kill_tree "${pid}"
done
sleep 1
for pid in $(pgrep -f '[r]un_exp_replica\.sh' 2>/dev/null || true); do
  kill -KILL "${pid}" 2>/dev/null || true
done
REMOTE_SCRIPT

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
