#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_redis_overhead_worktree.sh"
ae_run_redis ae_overhead_cleanup.sh
