#!/bin/bash
AE_MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AE_REDIS_PROXY_COMMIT=${AE_REDIS_PROXY_COMMIT:-fbc0808fff79d84a28b4dea8097ec01bd95df418}
AE_REDIS_PROXY_WORKTREE=${AE_REDIS_PROXY_WORKTREE:-${AE_MAIN_DIR}/.ae-worktrees/redis-proxy-gap}

ae_redis_proxy_die() { echo "[FAIL] $*" >&2; exit 1; }

ae_prepare_redis_proxy() {
  git -C "${AE_MAIN_DIR}" fetch -q origin nsdi27-ae-redis-proxy-gap ||
    ae_redis_proxy_die "could not fetch Redis proxy branch"
  git -C "${AE_MAIN_DIR}" cat-file -e "${AE_REDIS_PROXY_COMMIT}^{commit}" ||
    ae_redis_proxy_die "Redis proxy source is unavailable"
  mkdir -p "$(dirname "${AE_REDIS_PROXY_WORKTREE}")"
  git -C "${AE_MAIN_DIR}" worktree prune
  if [ ! -e "${AE_REDIS_PROXY_WORKTREE}/.git" ]; then
    [ ! -e "${AE_REDIS_PROXY_WORKTREE}" ] ||
      ae_redis_proxy_die "${AE_REDIS_PROXY_WORKTREE} exists but is not a worktree"
    git -C "${AE_MAIN_DIR}" worktree add --detach \
      "${AE_REDIS_PROXY_WORKTREE}" "${AE_REDIS_PROXY_COMMIT}"
  else
    [ -z "$(git -C "${AE_REDIS_PROXY_WORKTREE}" status --porcelain)" ] ||
      ae_redis_proxy_die "Redis proxy worktree has local changes"
    git -C "${AE_REDIS_PROXY_WORKTREE}" checkout -q --detach \
      "${AE_REDIS_PROXY_COMMIT}"
  fi
  echo "  [ OK ] Redis proxy source ready"
  export LITELIB_MAIN_DIR="${AE_MAIN_DIR}"
  export LITELIB_WORKTREE_DIR="${AE_REDIS_PROXY_WORKTREE}"
}
