#!/bin/bash
# Figure 13 step 3: generate Figure13.pdf.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/ae_memcached_worktree.sh"
ae_run_memcached_script ae_fig13_plot.sh "$@"
