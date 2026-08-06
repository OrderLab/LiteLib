#!/bin/bash
set -euo pipefail
ROOT=${1:-}
[ -d "${ROOT}" ] || { echo "usage: $0 RESULTS_DIR" >&2; exit 2; }
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
PAPER=${AE_PAPER_DIR:-${HOME}/litesys-nsdi27}
OUT="${MAIN}/results/leveldb-recovery/processed"
FIGURES=${AE_FIGURES_DIR:-${MAIN}/figures}
PYTHON=${MAIN}/.venv/bin/python
mkdir -p "${OUT}" "${FIGURES}"
python3 "$(dirname "$0")/ae_recovery_analyze.py" \
  "${ROOT}" --output "${OUT}" --check
"${PYTHON}" "${PAPER}/plot/plot_leveldb_throughput.py" \
  -t 120 \
  -o "${FIGURES}/Figure12.pdf" \
  "${OUT}/full.stat.json" \
  "${OUT}/ebpf.stat.json" \
  "${OUT}/checkpoint.stat.json"
echo "  [ OK ] Figure 12 -> ${FIGURES}/Figure12.pdf"
echo "  [ OK ] LevelDB memory -> ${OUT}/memory.json"
