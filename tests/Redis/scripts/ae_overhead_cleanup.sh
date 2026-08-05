#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
for node in node1 node2 node3; do
  ssh "${node}" "'${REMOTE}/tests/Redis/scripts/ae_overhead_node.sh' cleanup" ||
    true
done
echo "  [ OK ] Redis runtime cleaned; builds/results preserved"
