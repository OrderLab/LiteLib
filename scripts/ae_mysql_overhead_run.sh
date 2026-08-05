#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_mysql_overhead_worktree.sh"
[ "$#" -eq 0 ] || { echo "Usage: $0" >&2; exit 2; }
ae_run_mysql ae_overhead_run.sh
