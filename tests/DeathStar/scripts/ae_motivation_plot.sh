#!/bin/bash
#
# Figure 1 & 2 (Section 2, Motivation) -- STEP 3 of 3: parse logs and plot.
#
# Turns the raw logs produced by ae_motivation_run.sh into the two figures:
#
#   Figure 1  deathstar_latency.pdf     client latency over time,
#                                       (a) Vanilla vs (b) LiteLib
#   Figure 2  deathstar_isolation.pdf   load/latency/throughput of the healthy
#                                       Memcached instance before vs after the
#                                       failure
#
# Usage:
#   ./ae_motivation_plot.sh [RESULTS_DIR]
#
# RESULTS_DIR defaults to the newest run under <repo>/results/motivation.
# Figures are written to <repo>/figures/.
#
# Verify the toolchain against the data behind the paper:
#   ./ae_motivation_plot.sh --check ~/OriginalRawData/DeathStar/20250409_data_motivation

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_common.sh
source "${SCRIPT_DIR}/ae_common.sh"

CHECK_MODE=0
RESULTS_DIR=""

while [ $# -gt 0 ]; do
  case "$1" in
  --check) CHECK_MODE=1 ;;
  -h | --help)
    sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  *) RESULTS_DIR=$1 ;;
  esac
  shift
done

if [ -z "${RESULTS_DIR}" ]; then
  RESULTS_DIR=$(ls -1d "${AE_RESULTS_DIR}"/motivation/*/ 2>/dev/null | sort | tail -1)
  [ -n "${RESULTS_DIR}" ] ||
    ae_die "no results found under ${AE_RESULTS_DIR}/motivation. Run ./ae_motivation_run.sh first."
fi
RESULTS_DIR=${RESULTS_DIR%/}
[ -d "${RESULTS_DIR}" ] || ae_die "no such directory: ${RESULTS_DIR}"

FIGURE_DIR=${AE_FIGURES_DIR}
if [ "${CHECK_MODE}" -eq 1 ]; then
  FIGURE_DIR="${AE_FIGURES_DIR}/validation"
fi

ae_info "results:  ${RESULTS_DIR}"
ae_info "figures:  ${FIGURE_DIR}"

ae_ensure_venv
mkdir -p "${FIGURE_DIR}"

# The parsing/plotting code is the paper's own, so the artifact and the paper
# cannot drift apart.
PLOT_LATENCY="${AE_PAPER_DIR}/plot/plot_deathstar_latency.py"
PLOT_ISOLATION="${AE_PAPER_DIR}/plot/plot_deathstar_isolation.py"
COLLECT="${SCRIPT_DIR}/ae_motivation_collect.py"

for f in "${PLOT_LATENCY}" "${PLOT_ISOLATION}"; do
  [ -f "${f}" ] || ae_die "missing plotting script: ${f}
     Set AE_PAPER_DIR to the checkout of the paper repository."
done

# ---------------------------------------------------------------------------
# Pick representative crash runs for Figure 1
# ---------------------------------------------------------------------------

CRASH_DIR="${RESULTS_DIR}/crash"
NOCRASH_DIR="${RESULTS_DIR}/nocrash"
[ -d "${CRASH_DIR}" ] || ae_die "expected ${CRASH_DIR} to exist"

selection=$(ae_python "${SCRIPT_DIR}/ae_motivation_select.py" "${CRASH_DIR}") ||
  ae_die "could not select representative Figure 1 runs"
VANILLA_LOG=$(echo "${selection}" | awk -F '\t' '$1=="vanilla"{print $2}')
LITESYS_LOG=$(echo "${selection}" | awk -F '\t' '$1=="litesys"{print $2}')

[ -n "${VANILLA_LOG}" ] || ae_die "no vanilla client log in ${CRASH_DIR}"
[ -n "${LITESYS_LOG}" ] || ae_die "no litesys client log in ${CRASH_DIR}"

ae_info "Figure 1 inputs:"
echo "    vanilla: $(basename "${VANILLA_LOG}")"
echo "    litesys: $(basename "${LITESYS_LOG}")"

# ---------------------------------------------------------------------------
# Figure 1 -- client latency over time
# ---------------------------------------------------------------------------

FIG1="${FIGURE_DIR}/deathstar_latency.pdf"
SELECTED_DIR="${RESULTS_DIR}/selected"
mkdir -p "${SELECTED_DIR}"
ae_python - "${SELECTED_DIR}/runs.json" "${VANILLA_LOG}" "${LITESYS_LOG}" <<'PY'
import json, os, sys
with open(sys.argv[1], "w") as f:
    json.dump(
        {
            "vanilla": os.path.abspath(sys.argv[2]),
            "litesys": os.path.abspath(sys.argv[3]),
        },
        f,
        indent=2,
    )
PY
VANILLA_PLOT_LOG="${SELECTED_DIR}/vanilla.log"
LITESYS_PLOT_LOG="${SELECTED_DIR}/litesys.log"
ae_python "${SCRIPT_DIR}/ae_motivation_sanitize.py" \
  "${VANILLA_LOG}" "${VANILLA_PLOT_LOG}"
ae_python "${SCRIPT_DIR}/ae_motivation_sanitize.py" \
  "${LITESYS_LOG}" "${LITESYS_PLOT_LOG}"
# Delete first: otherwise a failed run leaves the previous PDF in place and the
# existence check below happily reports success on a stale figure.
rm -f "${FIG1}"
set -o pipefail
ae_python "${PLOT_LATENCY}" -o "${FIG1}" \
  "${VANILLA_PLOT_LOG}" "${LITESYS_PLOT_LOG}" 2>&1 |
  grep -v 'UserWarning\|plt.tight_layout'
plot_rc=$?
set +o pipefail
# grep exits 1 when it filters everything out; only a real failure matters.
if [ ! -s "${FIG1}" ]; then
  ae_die "failed to produce ${FIG1} (plotting exited ${plot_rc})"
fi
ae_ok "Figure 1 -> ${FIG1}"

# ---------------------------------------------------------------------------
# Figure 2 -- isolation of the healthy instance
# ---------------------------------------------------------------------------

STATS="${RESULTS_DIR}/stats.json"
if [ -n "$(find "${CRASH_DIR}" -name '*mcrouter*.log' 2>/dev/null | head -1)" ]; then
  collect_args=(--root "${RESULTS_DIR}" -o "${STATS}")
  if [ "${CHECK_MODE}" -eq 1 ]; then
    collect_args+=(--separate-nocrash)
  fi
  ae_python "${COLLECT}" "${collect_args[@]}" >/dev/null ||
    ae_die "failed to collect mcrouter stats"
  FIG2="${FIGURE_DIR}/deathstar_isolation.pdf"
  rm -f "${FIG2}"
  ae_python "${PLOT_ISOLATION}" -o "${FIG2}" "${STATS}" 2>&1 |
    grep -v 'UserWarning\|plt.tight_layout' || true
  if [ ! -s "${FIG2}" ]; then
    ae_warn "could not produce ${FIG2}; is the no-crash baseline present?"
  else
    ae_ok "Figure 2 -> ${FIG2}"
  fi
else
  ae_warn "no mcrouter logs in ${CRASH_DIR}; skipping Figure 2"
fi

# ---------------------------------------------------------------------------
# Report the trend the figures are supposed to show
# ---------------------------------------------------------------------------

echo
ae_info "Figure 1 trend check (this is the claim being reproduced)"
ae_python "${SCRIPT_DIR}/ae_motivation_trend.py" "${VANILLA_LOG}" "${LITESYS_LOG}"
TREND_RC=$?

if [ "${CHECK_MODE}" -eq 1 ]; then
  echo
  ae_info "--check: comparing the regenerated stats against the paper's data"
  PAPER_STATS="${AE_PAPER_DIR}/data/deathstar/stats.json"
  if [ -f "${STATS}" ] && [ -f "${PAPER_STATS}" ]; then
    ae_python - "${STATS}" "${PAPER_STATS}" <<'PY'
import json, sys
a = {k: {d['log_file']: d for d in v} for k, v in json.load(open(sys.argv[1])).items()}
b = {k: {d['log_file']: d for d in v} for k, v in json.load(open(sys.argv[2])).items()}
shared = 0
diff = 0
for sec in b:
    for lf, dv in b.get(sec, {}).items():
        if lf in a.get(sec, {}):
            shared += 1
            if json.dumps(dv, sort_keys=True) != json.dumps(a[sec][lf], sort_keys=True):
                diff += 1
                print(f"  DIFFERS: {sec}/{lf}")
print(f"  {shared} log(s) in common with the paper's stats.json, {diff} differing")
PY
  else
    ae_warn "no paper stats.json to compare against"
  fi
fi

echo
ae_ok "figures written to ${FIGURE_DIR}"
exit "${TREND_RC}"
