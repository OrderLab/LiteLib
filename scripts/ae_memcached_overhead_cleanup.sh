#!/bin/bash
# Stop/remove Figures 15/16 runtime state; preserve builds and results.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/ae_memcached_overhead_worktree.sh"
ae_run_overhead_script ae_overhead_cleanup.sh
