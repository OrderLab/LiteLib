#!/bin/bash
# Figure 13 step 2: run full, LiteLib and checkpoint with fixed paper settings.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "${AE_RUN_INNER:-0}" -ne 1 ]; then
  exec "${SCRIPT_DIR}/ae_run_with_retry.sh" \
    "${AE_RUN_TIMEOUT_SECONDS:-7200}" "${AE_RUN_MAX_ATTEMPTS:-2}" "$0" "$@"
fi
source "${SCRIPT_DIR}/ae_memcached_worktree.sh"
[ "$#" -eq 0 ] || { echo "Usage: $0" >&2; exit 2; }
ae_run_memcached_script ae_fig13_run.sh
