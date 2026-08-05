#!/bin/bash
# Generate Memcached's entries in Figures 14/15/16 (<1 min).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/ae_memcached_overhead_worktree.sh"

YCSB_DIR=${1:-}
if [ -z "${YCSB_DIR}" ]; then
  YCSB_DIR=$(find "${AE_MAIN_DIR}/results/memcached-overhead" \
    -mindepth 1 -maxdepth 1 -type d -name 'ycsb-*' | sort | tail -1)
fi
[ -n "${YCSB_DIR}" ] ||
  ae_overhead_die "no live YCSB results found; run ae_memcached_overhead_run.sh"

DEATHSTAR_DIR=$(find "${AE_MAIN_DIR}/results/motivation" \
  -mindepth 1 -maxdepth 1 -type d \
  -exec test -s '{}/selected/runs.json' ';' -print 2>/dev/null |
  sort | tail -1)
FIG13_DIR=$(find "${AE_MAIN_DIR}/results/fig13" \
  -mindepth 1 -maxdepth 1 -type d \
  -exec test -s '{}/monitor_full.log' ';' \
  -exec test -s '{}/monitor_lite.log' ';' \
  -exec test -s '{}/monitor_checkpoint.log' ';' -print 2>/dev/null |
  sort | tail -1)

[ -z "${DEATHSTAR_DIR}" ] || export AE_DEATHSTAR_RESULTS="${DEATHSTAR_DIR}"
[ -z "${FIG13_DIR}" ] || export AE_FIG13_RESULTS="${FIG13_DIR}"
ae_run_overhead_script ae_overhead_plot.sh "${YCSB_DIR}"
