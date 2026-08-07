#!/bin/bash
# Regenerate Figures 15/16 from all available live overhead results.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAPER=${AE_PAPER_DIR:-${HOME}/litesys-nsdi27}
PYTHON="${ROOT}/.venv/bin/python"
OUT="${ROOT}/results/overhead"
FIGURES="${ROOT}/figures"
RUN_ID=$(date +%Y%m%d-%H%M%S)
NODE=$(hostname -s)
MEMCACHED="${ROOT}/results/memcached-overhead"
LEVELDB="${ROOT}/results/leveldb-overhead/processed/leveldb.json"
REDIS="${ROOT}/results/redis-overhead/processed/redis.json"
MYSQL="${ROOT}/results/mysql-overhead/processed/mysql.json"

base_latency="${MEMCACHED}/latency.json"
base_cpu="${MEMCACHED}/cpu.json"
[ -s "${base_latency}" ] ||
  base_latency="${PAPER}/data/overhead/latency.json"
[ -s "${base_cpu}" ] ||
  base_cpu="${PAPER}/data/overhead/cpu.json"

args=(
  --base-latency "${base_latency}"
  --base-cpu "${base_cpu}"
  --output "${OUT}"
)
[ ! -s "${LEVELDB}" ] || args+=(--leveldb "${LEVELDB}")
[ ! -s "${REDIS}" ] || args+=(--redis "${REDIS}")
[ ! -s "${MYSQL}" ] || args+=(--mysql "${MYSQL}")

mkdir -p "${OUT}" "${FIGURES}" "${ROOT}/logs"
"${PYTHON}" "${ROOT}/scripts/ae_overhead_merge.py" "${args[@]}"
"${PYTHON}" "${PAPER}/plot/plot_latency_overhead.py" \
  "${OUT}/latency.json" -o "${FIGURES}/Figure15.pdf" \
  >"${ROOT}/logs/${RUN_ID}-fig15-plot-${NODE}.log" 2>&1
"${PYTHON}" "${PAPER}/plot/plot_cpu_overhead.py" \
  "${OUT}/cpu.json" -o "${FIGURES}/Figure16.pdf" \
  >"${ROOT}/logs/${RUN_ID}-fig16-plot-${NODE}.log" 2>&1

echo "  [ OK ] Figure 15 -> ${FIGURES}/Figure15.pdf"
echo "  [ OK ] Figure 16 -> ${FIGURES}/Figure16.pdf"
