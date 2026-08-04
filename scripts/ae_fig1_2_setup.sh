#!/bin/bash
# Figures 1/2 step 1: automatically prepare the DeathStar worktree and
# environment. Usage: ./scripts/ae_fig1_2_setup.sh [deps sync build ...]

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_deathstar_worktree.sh
source "${SCRIPT_DIR}/ae_deathstar_worktree.sh"
ae_run_deathstar_script ae_motivation_setup.sh "$@"
