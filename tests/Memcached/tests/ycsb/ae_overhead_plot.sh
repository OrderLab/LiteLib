#!/bin/bash
# Generate Figures 14/15/16 with the Memcached entries recomputed from raw data.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
PAPER=${AE_PAPER_DIR:-${HOME}/litesys-nsdi27}
OUT=${AE_OVERHEAD_RESULTS:-${MAIN}/results/memcached-overhead}
FIGURES=${AE_FIGURES_DIR:-${MAIN}/figures}
PYTHON=${MAIN}/.venv/bin/python
YCSB_DIR=${1:-}
DEATHSTAR_DIR=${AE_DEATHSTAR_RESULTS:-}
FIG13_DIR=${AE_FIG13_RESULTS:-}
RUN_ID=$(date +%Y%m%d-%H%M%S)
NODE=$(hostname -s)

mkdir -p "${OUT}" "${FIGURES}"
collect_args=(--output "${OUT}")
[ -z "${YCSB_DIR}" ] || collect_args+=(--ycsb-dir "${YCSB_DIR}")
[ -z "${DEATHSTAR_DIR}" ] || collect_args+=(--deathstar-results "${DEATHSTAR_DIR}")
[ -z "${FIG13_DIR}" ] || collect_args+=(--fig13-results "${FIG13_DIR}")
"${PYTHON}" "${SCRIPT_DIR}/ae_overhead_collect.py" "${collect_args[@]}"

run_plot() {
  local name=$1
  shift
  local log="${MAIN}/logs/${RUN_ID}-${name}-${NODE}.log"
  if ! "$@" >"${log}" 2>&1; then
    tail -40 "${log}" >&2
    exit 1
  fi
}

run_plot fig14-plot \
  "${PYTHON}" "${PAPER}/plot/plot_memory_overhead.py" \
  "${OUT}/memory.json" -o "${FIGURES}/Figure14.pdf"
run_plot fig15-plot \
  "${PYTHON}" "${PAPER}/plot/plot_latency_overhead.py" \
  "${OUT}/latency.json" -o "${FIGURES}/Figure15.pdf"
run_plot fig16-plot \
  "${PYTHON}" "${PAPER}/plot/plot_cpu_overhead.py" \
  "${OUT}/cpu.json" -o "${FIGURES}/Figure16.pdf"

echo "  [ OK ] Figure 14 -> ${FIGURES}/Figure14.pdf"
echo "  [ OK ] Figure 15 -> ${FIGURES}/Figure15.pdf"
echo "  [ OK ] Figure 16 -> ${FIGURES}/Figure16.pdf"
