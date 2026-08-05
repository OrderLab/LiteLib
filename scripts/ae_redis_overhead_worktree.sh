#!/bin/bash
AE_MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AE_REDIS_COMMIT=${AE_REDIS_COMMIT:-4277eaa1fd6782e01d3181e3d938a5e7589c4d67}
AE_REDIS_WORKTREE=${AE_REDIS_WORKTREE:-${AE_MAIN_DIR}/.ae-worktrees/redis-overhead}

ae_redis_die() { echo "[FAIL] $*" >&2; exit 1; }

ae_prepare_redis() {
  git -C "${AE_MAIN_DIR}" fetch -q origin nsdi27-ae-redis-overhead ||
    ae_redis_die "could not fetch Redis overhead branch"
  git -C "${AE_MAIN_DIR}" cat-file -e "${AE_REDIS_COMMIT}^{commit}" ||
    ae_redis_die "pinned Redis overhead commit unavailable"
  mkdir -p "$(dirname "${AE_REDIS_WORKTREE}")"
  git -C "${AE_MAIN_DIR}" worktree prune
  if [ ! -e "${AE_REDIS_WORKTREE}/.git" ]; then
    [ ! -e "${AE_REDIS_WORKTREE}" ] ||
      ae_redis_die "${AE_REDIS_WORKTREE} exists but is not a worktree"
    git -C "${AE_MAIN_DIR}" worktree add --detach \
      "${AE_REDIS_WORKTREE}" "${AE_REDIS_COMMIT}"
  else
    [ -z "$(git -C "${AE_REDIS_WORKTREE}" status --porcelain)" ] ||
      ae_redis_die "managed Redis worktree has local changes"
    git -C "${AE_REDIS_WORKTREE}" checkout -q --detach "${AE_REDIS_COMMIT}"
  fi
  echo "  [ OK ] Redis overhead commit: $(git -C "${AE_REDIS_WORKTREE}" rev-parse --short HEAD)"
  export LITELIB_MAIN_DIR="${AE_MAIN_DIR}"
  export LITELIB_WORKTREE_DIR="${AE_REDIS_WORKTREE}"
}

ae_run_redis() {
  local script=$1
  shift
  ae_prepare_redis
  local root="${AE_REDIS_WORKTREE}/tests/Redis/scripts"
  cd "${root}"
  exec "${root}/${script}" "$@"
}
