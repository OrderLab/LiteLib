#!/bin/bash
# Process the Memcached entries consumed by the combined Figure 14/15/16 plots.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OVERHEAD_RESULTS:-${MAIN}/results/memcached-overhead}
PYTHON=${MAIN}/.venv/bin/python
YCSB_DIR=${1:-}
DEATHSTAR_DIR=${AE_DEATHSTAR_RESULTS:-}
FIG13_DIR=${AE_FIG13_RESULTS:-}

mkdir -p "${OUT}"
collect_args=(--output "${OUT}")
[ -z "${YCSB_DIR}" ] || collect_args+=(--ycsb-dir "${YCSB_DIR}")
[ -z "${DEATHSTAR_DIR}" ] || collect_args+=(--deathstar-results "${DEATHSTAR_DIR}")
[ -z "${FIG13_DIR}" ] || collect_args+=(--fig13-results "${FIG13_DIR}")
"${PYTHON}" "${SCRIPT_DIR}/ae_overhead_collect.py" "${collect_args[@]}"
echo "  [ OK ] Memcached inputs -> ${OUT}/{memory,latency,cpu}.json"
