#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "${AE_RUN_INNER:-0}" -ne 1 ]; then
  exec "${SCRIPT_DIR}/ae_run_with_retry.sh" \
    "${AE_RUN_TIMEOUT_SECONDS:-7200}" "${AE_RUN_MAX_ATTEMPTS:-2}" "$0" "$@"
fi
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT=${AE_OUTPUT_DIR:-${ROOT}/results/table2/$(date +%Y%m%d-%H%M%S)}
mkdir -p "${OUT}"

source "${SCRIPT_DIR}/ae_redis_overhead_worktree.sh"
ae_prepare_redis
(
  cd "${AE_REDIS_WORKTREE}/tests/Redis/scripts"
  AE_OUTPUT_DIR="${OUT}/redis" ./ae_service_gap_run.sh
)

source "${SCRIPT_DIR}/ae_redis_proxy_gap_worktree.sh"
ae_prepare_redis_proxy
(
  cd "${AE_REDIS_PROXY_WORKTREE}/tests/Redis/scripts"
  LITELIB_REDIS_WORKTREE_DIR="${AE_REDIS_WORKTREE}" \
    AE_OUTPUT_DIR="${OUT}/redis-proxy" ./ae_service_gap_run.sh
)

source "${SCRIPT_DIR}/ae_mysql_overhead_worktree.sh"
ae_prepare_mysql
(
  cd "${AE_MYSQL_WORKTREE}/tests/MySQL/src/tests/scripts"
  AE_OUTPUT_DIR="${OUT}/mysql" ./ae_service_gap_run.sh
)

"${SCRIPT_DIR}/ae_table2_collect.sh" "${OUT}"
echo "  [ OK ] Table 2 raw results -> ${OUT}"
