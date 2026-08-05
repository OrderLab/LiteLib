#!/bin/bash
# Memcached Figures 15/16: 10 repetitions of all modes (~60–90 min).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/ae_memcached_overhead_worktree.sh"
[ "$#" -eq 0 ] || { echo "Usage: $0" >&2; exit 2; }
ae_run_overhead_script ae_overhead_run.sh
