#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_mysql_overhead_worktree.sh"
ae_run_mysql ae_overhead_setup.sh
