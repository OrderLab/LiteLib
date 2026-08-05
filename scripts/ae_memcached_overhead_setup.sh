#!/bin/bash
# Memcached Figures 15/16 one-time exact-commit build (~20–40 min).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/ae_memcached_overhead_worktree.sh"
ae_run_overhead_script ae_overhead_setup.sh
