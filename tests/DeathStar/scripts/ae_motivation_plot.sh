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

ae_info "results:  ${RESULTS_DIR}"
ae_info "figures:  ${AE_FIGURES_DIR}"

ae_ensure_venv
mkdir -p "${AE_FIGURES_DIR}"

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
# Pick the representative crash run for Figure 1
# ---------------------------------------------------------------------------

pick_client_log() {
  # <dir> <prefix> -- newest client log for that configuration.  Client logs are
  # "<type>_<timestamp>.log"; the per-component logs all carry an extra suffix
  # (.mcrouter.log, .memcached.N.log, ...), which the character class excludes.
  local dir=$1 prefix=$2
  find "${dir}" -maxdepth 1 -name "${prefix}_[0-9]*.log" 2>/dev/null |
    grep -E "/${prefix}_[0-9]{8}_[0-9]{6}\.log$" | sort | tail -1
}

CRASH_DIR="${RESULTS_DIR}/crash"
NOCRASH_DIR="${RESULTS_DIR}/nocrash"
[ -d "${CRASH_DIR}" ] || ae_die "expected ${CRASH_DIR} to exist"

VANILLA_LOG=$(pick_client_log "${CRASH_DIR}" vanilla)
LITESYS_LOG=$(pick_client_log "${CRASH_DIR}" litesys)

[ -n "${VANILLA_LOG}" ] || ae_die "no vanilla client log in ${CRASH_DIR}"
[ -n "${LITESYS_LOG}" ] || ae_die "no litesys client log in ${CRASH_DIR}"

ae_info "Figure 1 inputs:"
echo "    vanilla: $(basename "${VANILLA_LOG}")"
echo "    litesys: $(basename "${LITESYS_LOG}")"

# ---------------------------------------------------------------------------
# Figure 1 -- client latency over time
# ---------------------------------------------------------------------------

FIG1="${AE_FIGURES_DIR}/deathstar_latency.pdf"
ae_python "${PLOT_LATENCY}" -o "${FIG1}" "${VANILLA_LOG}" "${LITESYS_LOG}" 2>&1 |
  grep -v 'UserWarning\|plt.tight_layout' || true
[ -s "${FIG1}" ] || ae_die "failed to produce ${FIG1}"
ae_ok "Figure 1 -> ${FIG1}"

# ---------------------------------------------------------------------------
# Figure 2 -- isolation of the healthy instance
# ---------------------------------------------------------------------------

STATS="${RESULTS_DIR}/stats.json"
if [ -d "${NOCRASH_DIR}" ]; then
  ae_python "${COLLECT}" --root "${RESULTS_DIR}" -o "${STATS}" >/dev/null ||
    ae_die "failed to collect mcrouter stats"
  FIG2="${AE_FIGURES_DIR}/deathstar_isolation.pdf"
  ae_python "${PLOT_ISOLATION}" -o "${FIG2}" "${STATS}" 2>&1 |
    grep -v 'UserWarning\|plt.tight_layout' || true
  [ -s "${FIG2}" ] || ae_die "failed to produce ${FIG2}"
  ae_ok "Figure 2 -> ${FIG2}"
else
  ae_warn "no ${NOCRASH_DIR}; skipping Figure 2 (it needs the no-crash baseline)"
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
ae_ok "figures written to ${AE_FIGURES_DIR}"
exit "${TREND_RC}"
