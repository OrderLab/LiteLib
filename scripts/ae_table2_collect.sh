#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT=${1:-$(find "${ROOT}/results/table2" -mindepth 1 -maxdepth 1 \
  -type d -name '20*' | sort | tail -1)}
[ -d "${OUT}" ] || { echo "[FAIL] no Table 2 results" >&2; exit 1; }

python3 "${SCRIPT_DIR}/ae_table2_collect.py" \
  "${OUT}/redis/redis.csv" \
  "${OUT}/redis-proxy/redis-proxy.csv" \
  "${OUT}/mysql/mysql.csv" \
  --output "${ROOT}/figures/Table2.csv"
echo "  [ OK ] Table 2 -> ${ROOT}/figures/Table2.csv"
