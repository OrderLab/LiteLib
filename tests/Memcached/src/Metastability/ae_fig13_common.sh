#!/bin/bash
# Shared paths/helpers for Figure 13. This file is sourced.

FIG13_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -z "${LITELIB_WORKTREE_DIR:-}" ]; then
  LITELIB_WORKTREE_DIR=$(git -C "${FIG13_DIR}" rev-parse --show-toplevel)
fi
if [ -z "${LITELIB_MAIN_DIR:-}" ]; then
  common=$(git -C "${FIG13_DIR}" rev-parse --git-common-dir)
  LITELIB_MAIN_DIR=$(cd "${FIG13_DIR}" && cd "$(dirname "${common}")" && pwd)
fi

FIG13_RESULTS_DIR=${FIG13_RESULTS_DIR:-${LITELIB_MAIN_DIR}/results/fig13}
FIG13_FIGURES_DIR=${FIG13_FIGURES_DIR:-${LITELIB_MAIN_DIR}/figures}
FIG13_LOGS_DIR=${FIG13_LOGS_DIR:-${LITELIB_MAIN_DIR}/logs}
FIG13_VENV_DIR=${FIG13_VENV_DIR:-${LITELIB_MAIN_DIR}/.venv}
FIG13_PAPER_DIR=${FIG13_PAPER_DIR:-${LITELIB_MAIN_DIR}}
FIG13_STATE_DIR=${FIG13_STATE_DIR:-${FIG13_DIR}/.ae-state}

mkdir -p "${FIG13_RESULTS_DIR}" "${FIG13_FIGURES_DIR}" \
  "${FIG13_LOGS_DIR}" "${FIG13_STATE_DIR}"

fig13_info() { echo "==> $*"; }
fig13_ok() { echo "  [ OK ] $*"; }
fig13_die() { echo "  [FAIL] $*" >&2; exit 1; }

fig13_docker() {
  if docker info >/dev/null 2>&1; then
    docker "$@"
  else
    sudo -n docker "$@"
  fi
}

fig13_compose() {
  fig13_docker compose -f "${FIG13_DIR}/compose.yml" "$@"
}

fig13_python() {
  [ -x "${FIG13_VENV_DIR}/bin/python" ] ||
    fig13_die "plotting environment missing; run the base AE setup first"
  "${FIG13_VENV_DIR}/bin/python" "$@"
}

fig13_run_id() { date +%Y%m%d-%H%M%S; }
