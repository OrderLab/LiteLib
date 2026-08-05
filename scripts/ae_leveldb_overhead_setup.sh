#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/ae_leveldb_overhead_worktree.sh"
ae_run_leveldb ae_overhead_setup.sh
