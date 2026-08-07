#!/bin/bash
AE_MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AE_MYSQL_COMMIT=${AE_MYSQL_COMMIT:-be268c34876aff542b959d049de7be018fa6d6a0}
AE_MYSQL_WORKTREE=${AE_MYSQL_WORKTREE:-${AE_MAIN_DIR}/.ae-worktrees/mysql-overhead}

ae_mysql_die() { echo "[FAIL] $*" >&2; exit 1; }

ae_prepare_mysql() {
  git -C "${AE_MAIN_DIR}" fetch -q origin nsdi27-ae-mysql-overhead ||
    ae_mysql_die "could not fetch MySQL overhead branch"
  git -C "${AE_MAIN_DIR}" cat-file -e "${AE_MYSQL_COMMIT}^{commit}" ||
    ae_mysql_die "MySQL overhead source is unavailable"
  mkdir -p "$(dirname "${AE_MYSQL_WORKTREE}")"
  git -C "${AE_MAIN_DIR}" worktree prune
  if [ ! -e "${AE_MYSQL_WORKTREE}/.git" ]; then
    [ ! -e "${AE_MYSQL_WORKTREE}" ] ||
      ae_mysql_die "${AE_MYSQL_WORKTREE} exists but is not a worktree"
    git -C "${AE_MAIN_DIR}" worktree add --detach \
      "${AE_MYSQL_WORKTREE}" "${AE_MYSQL_COMMIT}"
  else
    [ -z "$(git -C "${AE_MYSQL_WORKTREE}" status --porcelain)" ] ||
      ae_mysql_die "managed MySQL worktree has local changes"
    git -C "${AE_MYSQL_WORKTREE}" checkout -q --detach "${AE_MYSQL_COMMIT}"
  fi
  echo "  [ OK ] MySQL overhead source ready"
  export LITELIB_MAIN_DIR="${AE_MAIN_DIR}"
  export LITELIB_WORKTREE_DIR="${AE_MYSQL_WORKTREE}"
}

ae_run_mysql() {
  local script=$1
  shift
  ae_prepare_mysql
  local root="${AE_MYSQL_WORKTREE}/tests/MySQL/src/tests/scripts"
  cd "${root}"
  exec "${root}/${script}" "$@"
}
