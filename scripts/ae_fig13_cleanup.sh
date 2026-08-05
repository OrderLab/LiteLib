#!/bin/bash
# Stop/remove Figure 13 runtime state; preserve mysql_data/archive and results.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/ae_memcached_worktree.sh"
ae_run_memcached_script ae_fig13_cleanup.sh
