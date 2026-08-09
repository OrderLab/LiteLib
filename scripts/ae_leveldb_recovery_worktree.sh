#!/bin/bash
AE_MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AE_LEVELDB_RECOVERY_COMMIT=${AE_LEVELDB_RECOVERY_COMMIT:-9035929fdffb74c312eeb045a6b9c8ecb9fe8be6}
AE_LEVELDB_RECOVERY_WORKTREE=${AE_LEVELDB_RECOVERY_WORKTREE:-${AE_MAIN_DIR}/.ae-worktrees/leveldb-recovery}

ae_leveldb_recovery_die(){ echo "[FAIL] $*" >&2; exit 1; }
ae_prepare_leveldb_recovery(){
  git -C "${AE_MAIN_DIR}" fetch -q origin nsdi27-ae-leveldb-recovery
  git -C "${AE_MAIN_DIR}" cat-file -e \
    "${AE_LEVELDB_RECOVERY_COMMIT}^{commit}" ||
    ae_leveldb_recovery_die "LevelDB recovery source unavailable"
  mkdir -p "$(dirname "${AE_LEVELDB_RECOVERY_WORKTREE}")"
  git -C "${AE_MAIN_DIR}" worktree prune
  if [ ! -e "${AE_LEVELDB_RECOVERY_WORKTREE}/.git" ]; then
    git -C "${AE_MAIN_DIR}" worktree add --detach \
      "${AE_LEVELDB_RECOVERY_WORKTREE}" "${AE_LEVELDB_RECOVERY_COMMIT}"
  else
    [ -z "$(git -C "${AE_LEVELDB_RECOVERY_WORKTREE}" status --porcelain)" ] ||
      ae_leveldb_recovery_die "managed LevelDB recovery worktree has local changes"
    git -C "${AE_LEVELDB_RECOVERY_WORKTREE}" checkout -q --detach \
      "${AE_LEVELDB_RECOVERY_COMMIT}"
  fi
  echo "  [ OK ] LevelDB recovery source ready"
  export LITELIB_MAIN_DIR="${AE_MAIN_DIR}"
  export LITELIB_WORKTREE_DIR="${AE_LEVELDB_RECOVERY_WORKTREE}"
}
ae_run_leveldb_recovery(){
  local script=$1; shift
  ae_prepare_leveldb_recovery
  local root="${AE_LEVELDB_RECOVERY_WORKTREE}/tests/LevelDB/scripts"
  cd "${root}"; exec "${root}/${script}" "$@"
}
