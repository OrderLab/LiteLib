#!/bin/bash
#
# Shared helpers for the LiteLib artifact-evaluation drivers.
#
# This file is *sourced*, never executed.
#
# Its main job is to keep every artifact the evaluation produces inside the
# repository instead of scattering it across $HOME and /tmp.  All output goes
# to the **main worktree**, so results stay in one place no matter which
# experiment branch (and therefore which `git worktree`) produced them:
#
#   <main worktree>/results/<experiment>/<run-id>/   raw experiment output
#   <main worktree>/figures/                         generated figures
#   <main worktree>/logs/                            setup/orchestration logs
#   <main worktree>/.venv/                           Python env for plotting
#
# All of those are git-ignored.

# --- Locate the repository ---------------------------------------------------

AE_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# `git rev-parse --git-common-dir` points at the *shared* .git directory, so
# this resolves to the main worktree even when called from a linked worktree.
_ae_git_common=$(git -C "${AE_SCRIPT_DIR}" rev-parse --git-common-dir 2>/dev/null)
if [ -n "${_ae_git_common}" ]; then
  _ae_git_common=$(cd "${AE_SCRIPT_DIR}" && cd "$(dirname "${_ae_git_common}")" && pwd)
fi
LITELIB_MAIN_DIR=${LITELIB_MAIN_DIR:-${_ae_git_common:-$(cd "${AE_SCRIPT_DIR}/../../.." && pwd)}}
unset _ae_git_common

# Root of *this* checkout (the worktree the caller is running from).
if [ -z "${LITELIB_WORKTREE_DIR:-}" ]; then
  LITELIB_WORKTREE_DIR=$(git -C "${AE_SCRIPT_DIR}" rev-parse --show-toplevel 2>/dev/null)
  if [ -z "${LITELIB_WORKTREE_DIR}" ]; then
    LITELIB_WORKTREE_DIR=$(cd "${AE_SCRIPT_DIR}/../../.." && pwd)
  fi
fi

# --- Output locations --------------------------------------------------------

AE_RESULTS_DIR=${AE_RESULTS_DIR:-${LITELIB_MAIN_DIR}/results}
AE_FIGURES_DIR=${AE_FIGURES_DIR:-${LITELIB_MAIN_DIR}/figures}
AE_LOGS_DIR=${AE_LOGS_DIR:-${LITELIB_MAIN_DIR}/logs}
AE_VENV_DIR=${AE_VENV_DIR:-${LITELIB_MAIN_DIR}/.venv}

# Where the paper's plotting scripts live.  Override if the paper repository is
# checked out somewhere else.
AE_PAPER_DIR=${AE_PAPER_DIR:-${LITELIB_MAIN_DIR}}

# --- Cluster ----------------------------------------------------------------

AE_NODES=${AE_NODES:-"node0 node1 node2 node3"}
AE_SSH_OPTS=${AE_SSH_OPTS:-"-o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10"}

# --- Pretty output -----------------------------------------------------------

if [ -t 1 ]; then
  AE_C_INFO=$'\033[1;34m'
  AE_C_OK=$'\033[32m'
  AE_C_BAD=$'\033[31m'
  AE_C_WARN=$'\033[33m'
  AE_C_OFF=$'\033[0m'
else
  AE_C_INFO=""
  AE_C_OK=""
  AE_C_BAD=""
  AE_C_WARN=""
  AE_C_OFF=""
fi

ae_info() { echo "${AE_C_INFO}==>${AE_C_OFF} $*"; }
ae_ok() { echo "  ${AE_C_OK}[ OK ]${AE_C_OFF} $*"; }
ae_warn() { echo "  ${AE_C_WARN}[WARN]${AE_C_OFF} $*"; }
ae_err() { echo "  ${AE_C_BAD}[FAIL]${AE_C_OFF} $*" 1>&2; }
ae_die() {
  ae_err "$*"
  exit 1
}

# --- Python environment for the plotting scripts -----------------------------

# ae_ensure_venv -- create ${AE_VENV_DIR} and install the paper's plotting
# requirements into it.  Idempotent: does nothing once the packages import.
ae_ensure_venv() {
  if [ -x "${AE_VENV_DIR}/bin/python" ] &&
    "${AE_VENV_DIR}/bin/python" -c 'import matplotlib, pandas, numpy' 2>/dev/null; then
    return 0
  fi
  ae_info "creating the Python environment for plotting in ${AE_VENV_DIR}"
  # python3-venv is not part of the base Ubuntu 22.04 image.
  if ! python3 -c 'import venv, ensurepip' 2>/dev/null; then
    sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq python3-venv python3-pip
  fi
  python3 -m venv "${AE_VENV_DIR}"
  local reqs="${AE_PAPER_DIR}/plot/requirements.txt"
  if [ -f "${reqs}" ]; then
    "${AE_VENV_DIR}/bin/pip" install -q -r "${reqs}"
  else
    ae_warn "${reqs} not found, installing plotting packages directly"
    "${AE_VENV_DIR}/bin/pip" install -q matplotlib pandas numpy
  fi
  "${AE_VENV_DIR}/bin/python" -c 'import matplotlib, pandas, numpy' ||
    ae_die "could not set up the plotting environment"
  ae_ok "plotting environment ready"
}

# ae_python -- run the plotting interpreter.
ae_python() {
  ae_ensure_venv
  "${AE_VENV_DIR}/bin/python" "$@"
}

# --- Misc helpers ------------------------------------------------------------

ae_run_id() { date +%Y%m%d-%H%M%S; }

# ae_rsh <node> <command...> -- run a command on a node (locally when it is us).
ae_rsh() {
  local node=$1
  shift
  if [ "${node}" = "$(hostname -s)" ]; then
    bash -c "$*"
  else
    # shellcheck disable=SC2086
    ssh ${AE_SSH_OPTS} "${node}" "$*"
  fi
}

# ae_require_cmd <cmd> <hint>
ae_require_cmd() {
  command -v "$1" >/dev/null 2>&1 ||
    ae_die "'$1' not found. $2"
}

mkdir -p "${AE_RESULTS_DIR}" "${AE_FIGURES_DIR}" "${AE_LOGS_DIR}" 2>/dev/null || true
