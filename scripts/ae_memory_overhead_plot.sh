#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAPER=${AE_PAPER_DIR:-${HOME}/litesys-nsdi27}
PYTHON="${ROOT}/.venv/bin/python"
BASE="${ROOT}/results/memcached-overhead/memory.json"
LEVELDB="${ROOT}/results/leveldb-recovery/processed/memory.json"
REDIS=${AE_REDIS_RESULTS:-$(find "${ROOT}/results/redis-overhead" \
  -mindepth 1 -maxdepth 1 -type d -name '20*' | sort | tail -1)}
MYSQL=${AE_MYSQL_RESULTS:-$(find "${ROOT}/results/mysql-overhead" \
  -mindepth 1 -maxdepth 1 -type d -name '20*' | sort | tail -1)}
OUT="${ROOT}/results/overhead/memory.json"
FIGURE="${ROOT}/figures/memory_overhead.pdf"

for path in "${BASE}" "${LEVELDB}"; do
  [ -s "${path}" ] || { echo "[FAIL] missing ${path}" >&2; exit 1; }
done
[ -d "${REDIS}" ] || { echo "[FAIL] no Redis results" >&2; exit 1; }
[ -d "${MYSQL}" ] || { echo "[FAIL] no MySQL results" >&2; exit 1; }

"${PYTHON}" "${ROOT}/scripts/ae_memory_overhead.py" \
  --base "${BASE}" \
  --leveldb "${LEVELDB}" \
  --redis "${REDIS}" \
  --mysql "${MYSQL}" \
  --output "${OUT}"
"${PYTHON}" "${PAPER}/plot/plot_memory_overhead.py" \
  "${OUT}" -o "${FIGURE}" \
  >"${ROOT}/logs/memory-overhead.log" 2>&1
echo "  [ OK ] Figure 14 -> ${FIGURE}"
