#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
for node in node0 node1 node2 node3; do
  ssh "${node}" "'${REMOTE}/tests/MySQL/src/tests/scripts/ae_overhead_node.sh' cleanup" ||
    true
done
echo "  [ OK ] MySQL runtime cleaned; builds/results preserved"
