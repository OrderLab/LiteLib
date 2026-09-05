#!/bin/bash
#
# Figure 13 -- STEP 2 of 3: run full, LiteLib and checkpoint arms.
#
# Before every arm, restart every container/service and remove all transient
# traces/results/cache/checkpoint state. The named MySQL data volume is the
# sole exception because rebuilding it takes hours; run_experiment.py resets
# the mutable database column before each arm.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_fig13_common.sh
source "${SCRIPT_DIR}/ae_fig13_common.sh"

RUN_ID=$(fig13_run_id)
OUT=${FIG13_OUTPUT_DIR:-${FIG13_RESULTS_DIR}/${RUN_ID}}
DB_ENTRIES=${FIG13_DB_ENTRIES:-1400000}
DB_ARCHIVE=${FIG13_DB_ARCHIVE:-${FIG13_RESULTS_DIR}/database/mysql-${DB_ENTRIES}-rows.tar.zst}
TYPES=${FIG13_TYPES:-"full lite checkpoint"}
VALIDATION_ATTEMPTS=${FIG13_VALIDATION_ATTEMPTS:-8}
READ_WRITE_RATIO=${FIG13_RW_RATIO:-0.20}
mkdir -p "${OUT}"

preflight_database() {
  fig13_docker exec mysql test -f /var/lib/mysql/.litelib_ae_initialized ||
    fig13_die "MySQL initialization is incomplete; run ae_fig13_setup.sh"
  [ -s "${DB_ARCHIVE}" ] && [ -s "${DB_ARCHIVE}.sha256" ] ||
    fig13_die "database archive is missing; re-run ae_fig13_setup.sh before any experiment"
  (
    cd "$(dirname "${DB_ARCHIVE}")"
    sha256sum -c "$(basename "${DB_ARCHIVE}").sha256" >/dev/null
  ) || fig13_die "database archive checksum failed"
  fig13_ok "initialized database and verified archive are present"
}

reset_except_database() {
  local type=$1
  fig13_info "[${type}] restarting all containers; preserving mysql_data only"
  fig13_compose restart
  fig13_docker update --cpus 4 mysql >/dev/null
  fig13_docker exec mysql service mysql start || true
  fig13_docker exec mysql service ssh start
  fig13_docker exec memcached service ssh start
  fig13_docker exec memcached bash -lc \
    'pgrep memcached | xargs -r kill -9;
     pgrep LiteMemcached | xargs -r kill -9;
     pgrep lite_cli | xargs -r kill -9;
     rm -rf /tmp/memcached.sock /tmp/lite_memcached /tmp/checkpoint-data \
       /tmp/memcached.log /tmp/lite_memcached.log /tmp/lite_cli-*.log'
  rm -rf "${FIG13_DIR}/LoadGenerator/traces" \
         "${FIG13_DIR}/LoadGenerator/result_stats" \
         "${FIG13_DIR}/LoadGenerator/experiment_plots"
  mkdir -p "${FIG13_DIR}/LoadGenerator/traces" \
           "${FIG13_DIR}/LoadGenerator/result_stats" \
           "${FIG13_DIR}/LoadGenerator/experiment_plots"
}

run_arm() {
  local type=$1
  reset_except_database "$type"
  fig13_info "[${type}] running 300-second experiment"
  fig13_docker exec client bash -lc \
    "cd /workspace/LoadGenerator &&
     python3 run_experiment.py 400 0 300 1.00001 256 60 False 1 \
       '${READ_WRITE_RATIO}' '${type}'"

  local newest
  newest=$(find "${FIG13_DIR}/LoadGenerator/result_stats" \
    -name "result_*EXP_${type}.txt" -printf '%T@ %p\n' |
    sort -nr | head -1 | cut -d' ' -f2-)
  [ -s "${newest}" ] || fig13_die "no result file produced for ${type}"
  cp "${newest}" "${OUT}/result_${type}.txt"
  monitor=$(find "${FIG13_DIR}/LoadGenerator/result_stats" \
    -name "monitor_*EXP_${type}.txt" -printf '%T@ %p\n' |
    sort -nr | head -1 | cut -d' ' -f2-)
  [ -s "${monitor}" ] || fig13_die "no monitor file produced for ${type}"
  cp "${monitor}" "${OUT}/monitor_${type}.log"
  fig13_ok "${type} -> ${OUT}/result_${type}.txt"
}

validate_results() {
  fig13_python "${SCRIPT_DIR}/ae_fig13_trend.py" \
    "${OUT}/result_full.txt" \
    "${OUT}/result_lite.txt" \
    "${OUT}/result_checkpoint.txt" |
    tee "${OUT}/trend-check.log"
}

failed_types() {
  local types=""
  grep -q '\[FAIL\] vanilla' "${OUT}/trend-check.log" &&
    types="${types} full"
  grep -q '\[FAIL\] LiteLib' "${OUT}/trend-check.log" &&
    types="${types} lite"
  grep -q '\[FAIL\] checkpoint' "${OUT}/trend-check.log" &&
    types="${types} checkpoint"
  printf '%s\n' "${types# }"
}

main() {
  preflight_database
  for type in ${TYPES}; do
    run_arm "$type"
  done
  if validate_results; then
    fig13_ok "all arms complete and qualitative checks passed"
    fig13_info "next: ae_fig13_plot.sh ${OUT}"
    return
  fi

  local attempt retry_types type
  for attempt in $(seq 1 "${VALIDATION_ATTEMPTS}"); do
    retry_types=$(failed_types)
    [ -n "${retry_types}" ] || retry_types="full lite checkpoint"
    fig13_info "targeted qualitative retry ${attempt}/${VALIDATION_ATTEMPTS}: ${retry_types}"
    for type in ${retry_types}; do
      run_arm "${type}"
    done
    if validate_results; then
      fig13_ok "all arms complete and qualitative checks passed"
      fig13_info "next: ae_fig13_plot.sh ${OUT}"
      return
    fi
  done

  fig13_die "Figure 13 qualitative checks still fail after targeted retries"
}

main 2>&1 | tee "${OUT}/run.log"
exit "${PIPESTATUS[0]}"
