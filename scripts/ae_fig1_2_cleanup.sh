#!/bin/bash
# Stop/remove all Figures 1/2 runtime state; preserve builds and results.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/ae_deathstar_worktree.sh"
ae_run_deathstar_script ae_motivation_cleanup.sh
