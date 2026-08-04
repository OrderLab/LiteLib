#!/bin/bash
# Figure 13 step 1: create/update managed worktree and initialize environment.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/ae_memcached_worktree.sh"
ae_run_memcached_script ae_fig13_setup.sh "$@"
