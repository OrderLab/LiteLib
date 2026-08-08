#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "${AE_RUN_INNER:-0}" -ne 1 ]; then
  exec "${SCRIPT_DIR}/ae_run_with_retry.sh" \
    "${AE_RUN_TIMEOUT_SECONDS:-7200}" "${AE_RUN_MAX_ATTEMPTS:-2}" "$0" "$@"
fi
source "${SCRIPT_DIR}/ae_redis_overhead_worktree.sh"
[ "$#" -eq 0 ] || { echo "Usage: $0" >&2; exit 2; }
ae_run_redis ae_overhead_run.sh
