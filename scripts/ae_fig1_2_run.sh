#!/bin/bash
# Figures 1/2 step 2: reset/recreate/prefill all state before every arm, then
# run with the author-calibrated c220g5 parameters.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_deathstar_worktree.sh
source "${SCRIPT_DIR}/ae_deathstar_worktree.sh"

if [ "$#" -ne 0 ]; then
  echo "This evaluator command uses fixed author-calibrated parameters." >&2
  echo "Usage: $0" >&2
  exit 2
fi

ae_run_deathstar_script ae_motivation_run.sh
