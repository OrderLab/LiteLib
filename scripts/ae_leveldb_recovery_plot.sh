#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_leveldb_recovery_worktree.sh"
ROOT=${1:-$(find "${AE_MAIN_DIR}/results/leveldb-recovery" \
  -mindepth 1 -maxdepth 1 -type d -name '20*' | sort | tail -1)}
[ -d "${ROOT}" ] || ae_leveldb_recovery_die "no LevelDB recovery results"
ae_run_leveldb_recovery ae_recovery_plot.sh "${ROOT}"
