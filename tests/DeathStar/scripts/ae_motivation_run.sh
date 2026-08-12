#!/bin/bash
#
# Figure 1 & 2 (Section 2, Motivation) -- STEP 2 of 3: run the experiment.
#
# Reproduces the motivation experiment: a DeathStarBench Social Network
# deployment where Mcrouter fronts two Memcached instances in active-active
# configuration, and one instance is killed 20 s into a 90 s run.
#
# For each configuration it runs:
#
#   crash    the failure is injected at t=20s   -> Figure 1 (and Figure 2 "after")
#   nocrash  the identical workload, no failure -> Figure 2 "before" baseline
#
# and each of those for both arms:
#
#   vanilla  stock memcached
#   litesys  LiteLib's embedded LiteMemcached alongside memcached
#
# Usage:
#   ./ae_motivation_run.sh [-n REPEATS] [-o RESULTS_DIR] [--only crash|nocrash]
#
# Defaults to 3 repetitions of each combination, matching the paper.
# Total runtime is roughly REPEATS x 4 x 3 minutes (~36 min for the default).
#
# Raw logs land in <repo>/results/motivation/<run-id>/{crash,nocrash}/ on the
# node you run this from, collected back from node3.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_common.sh
source "${SCRIPT_DIR}/ae_common.sh"

DEATHSTAR_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# Where run_exp_replica.sh and the container write their logs.
REMOTE_LOG_DIR="${DEATHSTAR_DIR}/src/socialNetwork/docker/lite-memcached/logs"
# The node that drives the experiment (it hosts Memcached and Mcrouter).
DRIVER_NODE=${AE_DRIVER_NODE:-node3}

REPEATS=3
ONLY=""
TYPES=${AE_MOTIVATION_TYPES:-"litesys vanilla"}
RESET_EVERY_RUN=${AE_RESET_EVERY_RUN:-1}
INITIAL_COOLDOWN=${AE_INITIAL_COOLDOWN:-30}
RUN_COOLDOWN=${AE_RUN_COOLDOWN:-0}
# CPU budget per Memcached instance (cgroup v2 "quota period").  See
# ae_motivation_calibrate.sh -- this sets the operating point of the experiment.
MEMCACHED_CPU_MAX=${MEMCACHED_CPU_MAX:-"50000 100000"}
# Calibrated once by the authors for the reference CloudLab c220g5 setup.
# Evaluators should use this fixed value rather than re-calibrating.
WORKLOAD_RATE=${WORKLOAD_RATE:-2700}
# Warm up at the same rate the measurement uses.  The warm-up determines what
# ends up cached, so warming at a different rate than the run silently changes
# the operating point -- and the calibration would no longer apply.
WARMUP_RATE=${WARMUP_RATE:-}
LITE_THREADS=${LITE_THREADS:-8}
LITE_CACHE_SIZE=${LITE_CACHE_SIZE:-${LITE_CACHE_ITEMS:-201326592}}
WORKLOAD_SEED=${WORKLOAD_SEED:-20250409}
RUN_ID=$(ae_run_id)
OUT_DIR=""

while [ $# -gt 0 ]; do
  case "$1" in
  -n | --repeats)
    REPEATS=$2
    shift
    ;;
  -o | --output)
    OUT_DIR=$2
    shift
    ;;
  --only)
    ONLY=$2
    shift
    ;;
  --types)
    TYPES=$2
    shift
    ;;
  --initial-cooldown)
    INITIAL_COOLDOWN=$2
    shift
    ;;
  --run-cooldown)
    RUN_COOLDOWN=$2
    shift
    ;;
  --reuse-state)
    RESET_EVERY_RUN=0
    ;;
  --cpu-max)
    MEMCACHED_CPU_MAX=$2
    shift
    ;;
  --rate)
    WORKLOAD_RATE=$2
    shift
    ;;
  --lite-threads)
    LITE_THREADS=$2
    shift
    ;;
  --lite-cache-size | --lite-cache-items)
    LITE_CACHE_SIZE=$2
    shift
    ;;
  -h | --help)
    sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  *) ae_die "unknown argument: $1 (try --help)" ;;
  esac
  shift
done

OUT_DIR=${OUT_DIR:-${AE_RESULTS_DIR}/motivation/${RUN_ID}}
mkdir -p "${OUT_DIR}/crash" "${OUT_DIR}/nocrash"
RUN_LOG="${OUT_DIR}/run.log"

RUN_TOTAL=0
RUN_FAILED=0
# Both figures come from the crash runs: Figure 2's "before" values are the
# t<20s segment of those same logs.  Separate no-crash runs add no information.
MODES=(crash)
[ -n "${ONLY}" ] && MODES=("${ONLY}")

ae_info "driver node: ${DRIVER_NODE}"
ae_info "repeats:     ${REPEATS} per configuration"
ae_info "modes:       ${MODES[*]}"
ae_info "arms:        ${TYPES} (LiteLib first avoids inheriting vanilla backlog)"
if [ "${RESET_EVERY_RUN}" -eq 1 ]; then
  ae_info "state reset: full redeploy + database prefill before EVERY run"
else
  ae_warn "state reset disabled (--reuse-state): debugging only; results are not comparable"
fi
ae_info "memcached CPU budget: ${MEMCACHED_CPU_MAX} (quota period)"
ae_info "offered load:         ${WORKLOAD_RATE} req/s (warm-up at the same rate)"
ae_info "workload seed:        ${WORKLOAD_SEED} (deterministic per thread)"
ae_info "LiteMemcached:         ${LITE_THREADS} threads, ${LITE_CACHE_SIZE} cached-value bytes"
ae_info "results:     ${OUT_DIR}"

# ---------------------------------------------------------------------------

preflight() {
  ae_info "pre-flight checks"
  ae_rsh "${DRIVER_NODE}" "test -x '${DEATHSTAR_DIR}/scripts/run_exp_replica.sh'" ||
    ae_die "run_exp_replica.sh not found on ${DRIVER_NODE}. Run ./ae_motivation_setup.sh first."
  # Both Memcached containers must be up, otherwise the run silently produces
  # a log with no failover activity at all.
  local n
  n=$(ae_rsh "${DRIVER_NODE}" "docker ps --format '{{.Names}}' | grep -c '^post-storage-memcached-'" 2>/dev/null || echo 0)
  [ "${n}" = "2" ] ||
    ae_die "expected 2 post-storage-memcached containers on ${DRIVER_NODE}, found ${n}.
     Run: ./ae_motivation_setup.sh deploy"
  ae_ok "2 Memcached containers up on ${DRIVER_NODE}"
}

# run_one <mode> <type> <iteration>
run_one() {
  local mode=$1 type=$2 iter=$3

  if [ "${RESET_EVERY_RUN}" -eq 1 ]; then
    ae_info "[${mode}] ${type} run ${iter}/${REPEATS}: resetting all service, database and cache state"
    # The DeathStar databases are small enough to recreate for every
    # repetition.  `deploy` removes/recreates every container (there are no
    # persistent database volumes in this compose file); `prefill` restores the
    # same social graph and post corpus.  start_memcached below then empties and
    # warms the two target caches explicitly.
    "${SCRIPT_DIR}/ae_motivation_setup.sh" deploy prefill ||
      return 1
  fi

  if [ "${INITIAL_COOLDOWN}" -gt 0 ]; then
    ae_info "waiting ${INITIAL_COOLDOWN}s after reset before warming the target caches"
    sleep "${INITIAL_COOLDOWN}"
  fi

  local prefix="${type}_$(date '+%Y%m%d_%H%M%S')"
  local no_crash=0
  [ "${mode}" = "nocrash" ] && no_crash=1

  ae_info "[${mode}] ${type} run ${iter}/${REPEATS} -> ${prefix}"

  # Clear the container-side log directory so the collection step can tell
  # exactly which files this run produced.
  ae_rsh "${DRIVER_NODE}" "mkdir -p '${REMOTE_LOG_DIR}' && rm -f '${REMOTE_LOG_DIR}/${prefix}.'*"

  # The client log is the driver's own stdout: run_exp_replica.sh runs under
  # `set -x`, so the log records the exact configuration alongside the wrk2
  # output the plotting scripts parse.
  ae_rsh "${DRIVER_NODE}" "
    cd '${DEATHSTAR_DIR}/scripts' &&
    LOG_PREFIX='${prefix}' NO_CRASH='${no_crash}' MEMCACHED_CPU_MAX='${MEMCACHED_CPU_MAX}' \
    WORKLOAD_RATE='${WORKLOAD_RATE}' WARMUP_RATE='${WARMUP_RATE:-${WORKLOAD_RATE}}' \
    WORKLOAD_SEED='${WORKLOAD_SEED}' LITE_THREADS='${LITE_THREADS}' \
    LITE_CACHE_SIZE='${LITE_CACHE_SIZE}' \
    ./run_exp_replica.sh ${type}
  " >"${OUT_DIR}/${mode}/${prefix}.log" 2>&1
  local rc=$?

  # Collect the per-component logs (mcrouter, memcached, monitors).
  # shellcheck disable=SC2029
  scp ${AE_SSH_OPTS} -q "${DRIVER_NODE}:${REMOTE_LOG_DIR}/${prefix}.*" \
    "${OUT_DIR}/${mode}/" 2>/dev/null || true

  local n
  n=$(find "${OUT_DIR}/${mode}" -name "${prefix}.*" | wc -l)
  if [ "${rc}" -ne 0 ]; then
    ae_err "run failed (exit ${rc}); see ${OUT_DIR}/${mode}/${prefix}.log"
    return 1
  fi
  if [ ! -s "${OUT_DIR}/${mode}/${prefix}.log" ]; then
    ae_err "empty client log for ${prefix}"
    return 1
  fi
  if [ "${type}" = "litesys" ]; then
    local lite_log="${OUT_DIR}/${mode}/${prefix}.lite_memcached.1.log"
    local mcrouter_log="${OUT_DIR}/${mode}/${prefix}.mcrouter.log"
    if [ "${mode}" = "crash" ] &&
      ! grep -q "Entered emergency mode" "${lite_log}" 2>/dev/null; then
      ae_err "LiteMemcached did not enter emergency mode for ${prefix}"
      return 1
    fi
    if [ ! -s "${mcrouter_log}" ]; then
      ae_err "missing mcrouter statistics for ${prefix}"
      return 1
    fi
    local max_failover
    max_failover=$(
      awk -F: '
        /"libmcrouter\.mcrouter\.11211\.failover_all":/ {
          gsub(/[ ,]/, "", $2)
          if ($2 + 0 > max) max = $2 + 0
          seen = 1
        }
        END {
          if (!seen) exit 1
          print max + 0
        }
      ' "${mcrouter_log}"
    ) || {
      ae_err "could not read mcrouter failover statistics for ${prefix}"
      return 1
    }
    if [ "${max_failover}" != "0" ]; then
      ae_err "LiteLib allowed ${max_failover} mcrouter failover(s) in ${prefix}"
      return 1
    fi
    if ! python3 "${SCRIPT_DIR}/ae_motivation_litesys_check.py" \
      "${OUT_DIR}/${mode}/${prefix}.log"; then
      ae_err "LiteLib did not recover pre-failure latency in ${prefix}"
      return 1
    fi
    if [ "${mode}" = "crash" ]; then
      ae_ok "LiteLib emergency mode served the run with zero mcrouter failover"
    else
      ae_ok "LiteLib no-crash control remained stable with zero mcrouter failover"
    fi
  fi
  ae_ok "collected ${n} component log(s) for ${prefix}"
  if [ "${RUN_COOLDOWN}" -gt 0 ]; then
    sleep "${RUN_COOLDOWN}"
  fi
}

main() {
  preflight

  local mode type i rc=0
  for mode in "${MODES[@]}"; do
    # Run every LiteLib repetition before vanilla.  Vanilla is intentionally
    # the arm that creates a prolonged cascade, so putting it first poisons the
    # pre-failure baseline of the next LiteLib run.
    for type in ${TYPES}; do
      for i in $(seq 1 "${REPEATS}"); do
        RUN_TOTAL=$((RUN_TOTAL + 1))
        run_one "${mode}" "${type}" "${i}" || {
          RUN_FAILED=$((RUN_FAILED + 1))
          rc=1
        }
      done
    done
  done

  echo
  if [ "${RUN_FAILED}" -eq 0 ]; then
    ae_ok "all ${RUN_TOTAL} runs completed"
  else
    ae_err "${RUN_FAILED}/${RUN_TOTAL} runs failed"
  fi
  ae_info "raw logs: ${OUT_DIR}"
  ae_info "next:     ./ae_motivation_plot.sh"
  return "${rc}"
}

main 2>&1 | tee -a "${RUN_LOG}"
exit "${PIPESTATUS[0]}"
