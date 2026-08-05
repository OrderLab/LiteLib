#!/bin/bash
# Managed-worktree helper for Figure 13.

AE_MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AE_MEMCACHED_COMMIT=${AE_MEMCACHED_COMMIT:-6296ad02f005205c7093e2847db6693c3611602a}
AE_MEMCACHED_WORKTREE=${AE_MEMCACHED_WORKTREE:-${AE_MAIN_DIR}/.ae-worktrees/memcached}

ae_memcached_die() { echo "[FAIL] $*" >&2; exit 1; }

ae_prepare_memcached_worktree() {
  echo "==> Preparing Memcached source"
  git -C "${AE_MAIN_DIR}" fetch -q origin nsdi27-ae-memcached ||
    ae_memcached_die "could not fetch origin/nsdi27-ae-memcached"
  git -C "${AE_MAIN_DIR}" cat-file -e "${AE_MEMCACHED_COMMIT}^{commit}" ||
    ae_memcached_die "pinned Memcached commit is unavailable"
  mkdir -p "$(dirname "${AE_MEMCACHED_WORKTREE}")"
  git -C "${AE_MAIN_DIR}" worktree prune

  if [ ! -e "${AE_MEMCACHED_WORKTREE}/.git" ]; then
    [ ! -e "${AE_MEMCACHED_WORKTREE}" ] ||
      ae_memcached_die "${AE_MEMCACHED_WORKTREE} exists but is not a worktree"
    git -C "${AE_MAIN_DIR}" worktree add --detach \
      "${AE_MEMCACHED_WORKTREE}" "${AE_MEMCACHED_COMMIT}" ||
      ae_memcached_die "could not create Memcached worktree"
  else
    [ -z "$(git -C "${AE_MEMCACHED_WORKTREE}" status --porcelain)" ] ||
      ae_memcached_die "managed Memcached worktree has local changes"
    git -C "${AE_MEMCACHED_WORKTREE}" checkout -q --detach \
      "${AE_MEMCACHED_COMMIT}" ||
      ae_memcached_die "could not update Memcached worktree"
  fi
  echo "  [ OK ] Memcached source ready"
  export LITELIB_MAIN_DIR="${AE_MAIN_DIR}"
  export LITELIB_WORKTREE_DIR="${AE_MEMCACHED_WORKTREE}"
}

ae_run_memcached_script() {
  local script=$1
  shift
  ae_prepare_memcached_worktree
  local root="${AE_MEMCACHED_WORKTREE}/tests/Memcached/src/Metastability"
  [ -x "${root}/${script}" ] || ae_memcached_die "missing ${root}/${script}"
  cd "${root}"
  exec "${root}/${script}" "$@"
}
