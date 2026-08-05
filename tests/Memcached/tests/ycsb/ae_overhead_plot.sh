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

mkdir -p "${OUT}" "${FIGURES}"
collect_args=(--output "${OUT}")
[ -z "${YCSB_DIR}" ] || collect_args+=(--ycsb-dir "${YCSB_DIR}")
"${PYTHON}" "${SCRIPT_DIR}/ae_overhead_collect.py" "${collect_args[@]}"
"${PYTHON}" "${PAPER}/plot/plot_memory_overhead.py" \
  "${OUT}/memory.json" -o "${FIGURES}/memory_overhead.pdf"
"${PYTHON}" "${PAPER}/plot/plot_latency_overhead.py" \
  "${OUT}/latency.json" -o "${FIGURES}/latency_overhead.pdf"
"${PYTHON}" "${PAPER}/plot/plot_cpu_overhead.py" \
  "${OUT}/cpu.json" -o "${FIGURES}/cpu_overhead.pdf"

echo "  [ OK ] Figure 14 -> ${FIGURES}/memory_overhead.pdf"
echo "  [ OK ] Figure 15 -> ${FIGURES}/latency_overhead.pdf"
echo "  [ OK ] Figure 16 -> ${FIGURES}/cpu_overhead.pdf"
