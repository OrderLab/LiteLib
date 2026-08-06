#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_leveldb_recovery_worktree.sh"
ae_run_leveldb_recovery ae_recovery_cleanup.sh
