#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
for node in node1 node2 node3; do
  ssh "${node}" "
    helper='${REMOTE}/tests/Redis/scripts/ae_overhead_node.sh'
    if [ -x \"\${helper}\" ]; then
      \"\${helper}\" cleanup
    else
      for name in redis-server redis-server-vanilla redis-sentinel-vanilla redis-lite; do
        for pid in \$(pgrep -x \"\${name}\" 2>/dev/null || true); do
          kill \"\${pid}\" 2>/dev/null || true
        done
      done
      rm -rf /tmp/litelib-ae-redis
      rm -f /tmp/redis.sock /tmp/lite_Redis /dev/shm/lite_shared_memory
    fi
  " || true
done
echo "  [ OK ] Redis runtime cleaned; builds/results preserved"
