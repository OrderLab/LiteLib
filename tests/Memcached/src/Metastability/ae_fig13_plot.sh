#!/bin/bash
#
# Figure 13 -- STEP 3 of 3: generate Figure13.pdf.
#
# Usage:
#   ./ae_fig13_plot.sh [RESULTS_DIR]
#   ./ae_fig13_plot.sh --check ~/OriginalRawData/Memcached
#
# The archived raw data is an overlay: v2 overrides matching settings in v1,
# and v1 overrides v0. For Figure 13 this resolves to full/checkpoint from v1
# and LiteLib from v2.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_fig13_common.sh
source "${SCRIPT_DIR}/ae_fig13_common.sh"

CHECK=0
ROOT=""
if [ "${1:-}" = "--check" ]; then
  CHECK=1
  ROOT=${2:-${HOME}/OriginalRawData/Memcached}
elif [ "$#" -gt 0 ]; then
  ROOT=$1
else
  ROOT=$(find "${FIG13_RESULTS_DIR}" -mindepth 1 -maxdepth 1 -type d |
    sort | tail -1)
fi
[ -d "${ROOT}" ] || fig13_die "no results directory: ${ROOT}"

if [ "${CHECK}" -eq 1 ]; then
  OUT="${FIG13_RESULTS_DIR}/archived-overlay"
  rm -rf "${OUT}"
  mkdir -p "${OUT}"
  for type in full lite checkpoint; do
    found=""
    for version in v2 v1 v0; do
      candidate=$(find "${ROOT}/${version}" -maxdepth 2 -type f \
        -name "*result*ARV_RATE_400.0*RW_RATIO_0.05*EXP_${type}.txt" |
        sort | tail -1)
      if [ -n "${candidate}" ]; then
        found=${candidate}
        break
      fi
    done
    [ -n "${found}" ] || fig13_die "could not resolve overlay input for ${type}"
    cp "${found}" "${OUT}/result_${type}.txt"
    echo "  ${type}: ${found}"
  done
  ROOT=${OUT}
fi

for type in full lite checkpoint; do
  [ -s "${ROOT}/result_${type}.txt" ] ||
    fig13_die "missing ${ROOT}/result_${type}.txt"
done

PLOT="${FIG13_PAPER_DIR}/plot/plot_memcached.py"
[ -f "${PLOT}" ] || fig13_die "missing paper plot script: ${PLOT}"
FIGURE_DIR=${FIG13_FIGURES_DIR}
if [ "${CHECK}" -eq 1 ]; then
  FIGURE_DIR="${FIG13_FIGURES_DIR}/validation"
fi
mkdir -p "${FIGURE_DIR}"
FIG="${FIGURE_DIR}/Figure13.pdf"
rm -f "${FIG}"
WINDOW_END=$(
  fig13_python "${SCRIPT_DIR}/ae_fig13_trend.py" --window \
    "${ROOT}/result_full.txt" \
    "${ROOT}/result_lite.txt" \
    "${ROOT}/result_checkpoint.txt"
)
fig13_python "${PLOT}" -r 400 -t "${WINDOW_END}" -o "${FIG}" \
  "${ROOT}/result_full.txt" \
  "${ROOT}/result_lite.txt" \
  "${ROOT}/result_checkpoint.txt"
[ -s "${FIG}" ] || fig13_die "failed to produce ${FIG}"
fig13_ok "Figure 13 -> ${FIG}"

fig13_python "${SCRIPT_DIR}/ae_fig13_trend.py" \
  "${ROOT}/result_full.txt" \
  "${ROOT}/result_lite.txt" \
  "${ROOT}/result_checkpoint.txt"

if [ "${CHECK}" -eq 1 ]; then
  for type in full lite checkpoint; do
    paper="${FIG13_PAPER_DIR}/data/memcached/result_${type}.txt"
    cmp -s "${ROOT}/result_${type}.txt" "${paper}" ||
      fig13_die "archived overlay differs from paper data for ${type}"
  done
  fig13_ok "archived overlay matches all three paper inputs byte-for-byte"
fi
