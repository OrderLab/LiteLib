#!/bin/bash
AE_MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AE_LEVELDB_COMMIT=${AE_LEVELDB_COMMIT:-34ccf34de3c6dc751bdef602cf4477af9ed1a31d}
AE_LEVELDB_WORKTREE=${AE_LEVELDB_WORKTREE:-${AE_MAIN_DIR}/.ae-worktrees/leveldb-overhead}

ae_leveldb_die(){ echo "[FAIL] $*" >&2; exit 1; }
ae_prepare_leveldb(){
  git -C "${AE_MAIN_DIR}" fetch -q origin nsdi27-ae-leveldb-overhead
  git -C "${AE_MAIN_DIR}" cat-file -e "${AE_LEVELDB_COMMIT}^{commit}" ||
    ae_leveldb_die "pinned LevelDB commit unavailable"
  mkdir -p "$(dirname "${AE_LEVELDB_WORKTREE}")"
  git -C "${AE_MAIN_DIR}" worktree prune
  if [ ! -e "${AE_LEVELDB_WORKTREE}/.git" ]; then
    git -C "${AE_MAIN_DIR}" worktree add --detach \
      "${AE_LEVELDB_WORKTREE}" "${AE_LEVELDB_COMMIT}"
  else
    [ -z "$(git -C "${AE_LEVELDB_WORKTREE}" status --porcelain)" ] ||
      ae_leveldb_die "managed LevelDB worktree has local changes"
    git -C "${AE_LEVELDB_WORKTREE}" checkout -q --detach "${AE_LEVELDB_COMMIT}"
  fi
  echo "  [ OK ] LevelDB overhead commit: $(git -C "${AE_LEVELDB_WORKTREE}" rev-parse --short HEAD)"
  export LITELIB_MAIN_DIR="${AE_MAIN_DIR}"
  export LITELIB_WORKTREE_DIR="${AE_LEVELDB_WORKTREE}"
}
ae_run_leveldb(){
  local script=$1; shift
  ae_prepare_leveldb
  local root="${AE_LEVELDB_WORKTREE}/tests/LevelDB/scripts"
  cd "${root}"; exec "${root}/${script}" "$@"
}
