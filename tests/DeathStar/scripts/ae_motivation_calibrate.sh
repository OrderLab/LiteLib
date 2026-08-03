#!/bin/bash
#
# Figure 1 & 2 -- STEP 1b (optional but usually necessary): calibrate the load.
#
# The effect Figure 1 shows is a *cascading* failure: when one of the two
# Memcached instances dies, Mcrouter fails its traffic over to the survivor,
# which then cannot keep up and collapses.  That only happens if the surviving
# instance is driven past its capacity by the extra ~65% of load.
#
# The load at which that happens is machine specific.  Two nodes of the same
# CloudLab type do not necessarily saturate at the same request rate, so a rate
# that reproduces the paper on one cluster can leave plenty of headroom on
# another -- in which case the failover is absorbed, latency stays flat, and
# the figure shows nothing.
#
# This script sweeps the offered load, runs the *vanilla* crash experiment at
# each point, and reports how the surviving instance behaved.  It picks the
# lowest rate at which the failure actually cascades.
#
# Usage:
#   ./ae_motivation_calibrate.sh [-r "2500 3500 4500"] [--cpu-max "100000 100000"]
#
# Each data point costs one ~3 minute run.  When it finishes it prints the
# WORKLOAD_RATE to use, which you then pass to ae_motivation_run.sh:
#
#   ./ae_motivation_run.sh --rate <calibrated>

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_common.sh
source "${SCRIPT_DIR}/ae_common.sh"

DEATHSTAR_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REMOTE_LOG_DIR="${DEATHSTAR_DIR}/src/socialNetwork/docker/lite-memcached/logs"
DRIVER_NODE=${AE_DRIVER_NODE:-node3}

RATES=${AE_CALIBRATE_RATES:-"2500"}
MEMCACHED_CPU_MAX=${MEMCACHED_CPU_MAX:-"100000 100000"}
# Optional sweep over the per-instance CPU budget instead of (or as well as)
# the offered load.  Values are cgroup v2 quotas against a 100000us period, so
# 50000 is half a core.  This is usually the more effective knob: the client
# side of the benchmark saturates before Memcached does, which caps how much
# load can be pushed through by raising -R alone.
CPU_QUOTAS=${AE_CALIBRATE_CPU_QUOTAS:-""}
# The surviving instance counts as saturated when the failover makes its
# service time blow up by at least this factor.
SATURATION_FACTOR=${AE_SATURATION_FACTOR:-10}

while [ $# -gt 0 ]; do
  case "$1" in
  -r | --rates)
    RATES=$2
    shift
    ;;
  --cpu-max)
    MEMCACHED_CPU_MAX=$2
    shift
    ;;
  --cpu-sweep)
    CPU_QUOTAS=$2
    shift
    ;;
  -h | --help)
    sed -n '2,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  *) ae_die "unknown argument: $1" ;;
  esac
  shift
done

OUT_DIR="${AE_RESULTS_DIR}/motivation-calibration/$(ae_run_id)"
mkdir -p "${OUT_DIR}"

ae_info "sweeping offered load: ${RATES} req/s"
ae_info "memcached CPU budget:  ${MEMCACHED_CPU_MAX}"
ae_info "results:               ${OUT_DIR}"
echo

printf '  %-8s %-10s %14s %14s %10s %12s\n' RATE CPU-QUOTA "pre-fail(ms)" "post-fail(ms)" FACTOR VERDICT
printf '  %-8s %-10s %14s %14s %10s %12s\n' -------- ---------- -------------- -------------- ---------- ------------

# Build the list of (rate, cpu-quota) points to try.
POINTS=()
if [ -n "${CPU_QUOTAS}" ]; then
  for q in ${CPU_QUOTAS}; do
    for r in ${RATES}; do POINTS+=("${r}:${q} 100000"); done
  done
else
  for r in ${RATES}; do POINTS+=("${r}:${MEMCACHED_CPU_MAX}"); done
fi

BEST_RATE=""
BEST_CPU=""
for point in "${POINTS[@]}"; do
  rate=${point%%:*}
  cpu_max=${point#*:}
  prefix="vanilla_$(date '+%Y%m%d_%H%M%S')"
  dir="${OUT_DIR}/rate-${rate}-cpu-${cpu_max%% *}/crash"
  mkdir -p "${dir}"

  ae_rsh "${DRIVER_NODE}" "mkdir -p '${REMOTE_LOG_DIR}' && rm -f '${REMOTE_LOG_DIR}/${prefix}.'*" >/dev/null 2>&1
  ae_rsh "${DRIVER_NODE}" "
    cd '${DEATHSTAR_DIR}/scripts' &&
    LOG_PREFIX='${prefix}' NO_CRASH=0 \
    MEMCACHED_CPU_MAX='${cpu_max}' \
    WORKLOAD_RATE='${rate}' WARMUP_RATE='${rate}' \
    ./run_exp_replica.sh vanilla
  " >"${dir}/${prefix}.log" 2>&1
  # shellcheck disable=SC2029
  scp ${AE_SSH_OPTS} -q "${DRIVER_NODE}:${REMOTE_LOG_DIR}/${prefix}.*" "${dir}/" 2>/dev/null || true

  read -r before after factor verdict <<<"$(
    AE_SCRIPTS="${SCRIPT_DIR}" ae_python - "${dir}" "${SATURATION_FACTOR}" <<'PY'
import glob, os, re, sys
sys.path.insert(0, os.environ["AE_SCRIPTS"])
d, need = sys.argv[1], float(sys.argv[2])
from ae_motivation_trend import parse_log_file

# Judge on the *client* latency: that is what Figure 1 plots and what the
# paper claims.  The mcrouter service time is a useful secondary signal but
# saturates at a different point.
cli = [f for f in glob.glob(os.path.join(d, "*.log"))
       if re.search(r"vanilla_\d{8}_\d{6}\.log$", f)]
if not cli:
    print("nan nan nan NO-LOG")
    raise SystemExit
df = parse_log_file(cli[0])
v = df[df["latency"].notna()]
pre = v[v["timestamp"] <= 20]["latency"].mean()
post = v[v["timestamp"] > 20]
if post.empty or not pre:
    print("nan nan nan NO-DATA")
    raise SystemExit
first = post[post["timestamp"] <= 40]["latency"].mean()
last = post[post["timestamp"] > post["timestamp"].max() - 20]["latency"].mean()
ratio = last / pre
# A usable operating point needs BOTH a healthy pre-failure baseline (the
# system must not already be saturated) and a clear post-failure blow-up.
if pre >= 500:
    verdict = "OVERLOADED"
elif ratio >= need and last >= first:
    verdict = "CASCADE"
else:
    verdict = "absorbed"
print(f"{pre:.0f} {last:.0f} {ratio:.1f} {verdict}")
PY
  )"

  printf '  %-8s %-10s %14s %14s %10s %12s\n' "${rate}" "${cpu_max%% *}" "${before}" "${after}" "${factor}x" "${verdict}"
  if [ "${verdict}" = "CASCADE" ] && [ -z "${BEST_RATE}" ]; then
    BEST_RATE=${rate}
    BEST_CPU=${cpu_max}
  fi
  sleep 10
done

echo
if [ -n "${BEST_RATE}" ]; then
  ae_ok "the failure cascades at rate=${BEST_RATE} req/s, CPU budget '${BEST_CPU}'"
  echo
  echo "    Run the experiment with:"
  echo "      ./ae_motivation_run.sh --rate ${BEST_RATE} --cpu-max \"${BEST_CPU}\""
  echo
  echo "    (record this in docs; the saturation point is cluster specific)"
else
  ae_warn "no rate in '${RATES}' produced a cascade."
  echo "    Either sweep higher rates:"
  echo "      $0 --rates \"$(echo ${RATES} | awk '{print $NF*1.5, $NF*2, $NF*3}')\""
  echo "    or give each Memcached instance less CPU:"
  echo "      $0 --cpu-max \"60000 100000\""
fi
ae_info "raw sweep data: ${OUT_DIR}"
