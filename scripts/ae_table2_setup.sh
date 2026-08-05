#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "${SCRIPT_DIR}/ae_redis_overhead_worktree.sh"
ae_prepare_redis
(
  cd "${AE_REDIS_WORKTREE}/tests/Redis/scripts"
  ./ae_overhead_setup.sh
)

source "${SCRIPT_DIR}/ae_redis_proxy_gap_worktree.sh"
ae_prepare_redis_proxy
(
  cd "${AE_REDIS_PROXY_WORKTREE}/tests/Redis/scripts"
  LITELIB_REDIS_WORKTREE_DIR="${AE_REDIS_WORKTREE}" \
    ./ae_service_gap_setup.sh
)

source "${SCRIPT_DIR}/ae_mysql_overhead_worktree.sh"
ae_prepare_mysql
(
  cd "${AE_MYSQL_WORKTREE}/tests/MySQL/src/tests/scripts"
  ./ae_overhead_setup.sh
)
echo "  [ OK ] Table 2 environment ready"
