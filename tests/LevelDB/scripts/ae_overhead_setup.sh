#!/bin/bash
# Pinned LevelDB overhead setup (commit 680892c8...).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
JOBS=${AE_JOBS:-32}

echo "==> Syncing pinned source to node0/node1"
for node in node0 node1; do
  [ "$node" = "$(hostname -s)" ] && continue
  ssh "$node" "mkdir -p '${REMOTE}'"
  rsync -az --delete --exclude .git --exclude build/ --exclude target/ \
    --exclude tmp-data/ \
    -e ssh "${ROOT}/" "${node}:${REMOTE}/"
done

echo "==> Running the documented server and client setup scripts"
ssh node0 "cd '${REMOTE}/tests/LevelDB/scripts' &&
  AE_JOBS='${JOBS}' ./server.sh" &
server_pid=$!
ssh node1 "cd '${REMOTE}/tests/LevelDB/scripts' && ./client.sh" &
client_pid=$!
wait "${server_pid}"
wait "${client_pid}"

echo "  [ OK ] LevelDB overhead environment ready"
