#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_leveldb_overhead_worktree.sh"
ROOT=${1:-$(find "${AE_MAIN_DIR}/results/leveldb-overhead" -mindepth 1 -maxdepth 1 -type d | sort | tail -1)}
[ -d "${ROOT}" ] || ae_leveldb_die "no LevelDB results"
ae_run_leveldb ae_overhead_plot.sh "${ROOT}"
