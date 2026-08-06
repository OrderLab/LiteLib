#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_leveldb_recovery_worktree.sh"
[ "$#" -eq 0 ] || { echo "Usage: $0" >&2; exit 2; }
ae_run_leveldb_recovery ae_recovery_run.sh
