#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_redis_overhead_worktree.sh"
ROOT=${1:-$(find "${AE_MAIN_DIR}/results/redis-overhead" \
  -mindepth 1 -maxdepth 1 -type d -name '20*' | sort | tail -1)}
[ -d "${ROOT}" ] || ae_redis_die "no Redis results"
ae_run_redis ae_overhead_plot.sh "${ROOT}"
