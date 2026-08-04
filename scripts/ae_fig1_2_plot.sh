#!/bin/bash
# Figures 1/2 step 3: select representative repetitions, parse logs and
# generate both PDFs. Usage: ./scripts/ae_fig1_2_plot.sh [RESULTS_DIR|--check]

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_deathstar_worktree.sh
source "${SCRIPT_DIR}/ae_deathstar_worktree.sh"
ae_run_deathstar_script ae_motivation_plot.sh "$@"
