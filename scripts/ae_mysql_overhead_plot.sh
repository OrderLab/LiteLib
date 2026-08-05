#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_mysql_overhead_worktree.sh"
ROOT=${1:-$(find "${AE_MAIN_DIR}/results/mysql-overhead" \
  -mindepth 1 -maxdepth 1 -type d -name '20*' | sort | tail -1)}
[ -d "${ROOT}" ] || ae_mysql_die "no MySQL results"
ae_run_mysql ae_overhead_plot.sh "${ROOT}"
