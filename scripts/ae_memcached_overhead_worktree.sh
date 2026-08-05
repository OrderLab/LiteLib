#!/bin/bash
# Managed worktree helper for Memcached's Figures 14/15/16.

AE_MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AE_OVERHEAD_COMMIT=${AE_OVERHEAD_COMMIT:-92336e6bce9dc3d59d9aeb54e05c66eb90f85775}
AE_OVERHEAD_WORKTREE=${AE_OVERHEAD_WORKTREE:-${AE_MAIN_DIR}/.ae-worktrees/memcached-overhead}

ae_overhead_die() { echo "[FAIL] $*" >&2; exit 1; }

ae_prepare_overhead_worktree() {
  git -C "${AE_MAIN_DIR}" fetch -q origin nsdi27-ae-memcached-overhead ||
    ae_overhead_die "could not fetch overhead branch"
  git -C "${AE_MAIN_DIR}" cat-file -e "${AE_OVERHEAD_COMMIT}^{commit}" ||
    ae_overhead_die "pinned overhead commit is unavailable"
  mkdir -p "$(dirname "${AE_OVERHEAD_WORKTREE}")"
  git -C "${AE_MAIN_DIR}" worktree prune
  if [ ! -e "${AE_OVERHEAD_WORKTREE}/.git" ]; then
    [ ! -e "${AE_OVERHEAD_WORKTREE}" ] ||
      ae_overhead_die "${AE_OVERHEAD_WORKTREE} exists but is not a worktree"
    git -C "${AE_MAIN_DIR}" worktree add --detach \
      "${AE_OVERHEAD_WORKTREE}" "${AE_OVERHEAD_COMMIT}"
  else
    [ -z "$(git -C "${AE_OVERHEAD_WORKTREE}" status --porcelain)" ] ||
      ae_overhead_die "managed overhead worktree has local changes"
    git -C "${AE_OVERHEAD_WORKTREE}" checkout -q --detach "${AE_OVERHEAD_COMMIT}"
  fi
  echo "  [ OK ] Memcached overhead commit: $(git -C "${AE_OVERHEAD_WORKTREE}" rev-parse --short HEAD)"
  export LITELIB_MAIN_DIR="${AE_MAIN_DIR}"
  export LITELIB_WORKTREE_DIR="${AE_OVERHEAD_WORKTREE}"
}

ae_run_overhead_script() {
  local script=$1
  shift
  ae_prepare_overhead_worktree
  local root="${AE_OVERHEAD_WORKTREE}/tests/Memcached/tests/ycsb"
  cd "${root}"
  exec "${root}/${script}" "$@"
}
