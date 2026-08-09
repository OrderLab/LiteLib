#!/bin/bash
#
# Shared helper for the Figure 1/2 wrappers.
#
# Evaluators stay on the nsdi27-ae branch. This helper automatically maintains
# a detached, managed worktree of the DeathStar AE branch inside LiteLib and
# invokes the experiment scripts there. Generated data still lands in the
# main checkout's results/, figures/ and logs/ directories.

AE_MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AE_DEATHSTAR_COMMIT=${AE_DEATHSTAR_COMMIT:-54426ebdfb739691aea8d0aed18e232b1a5f261d}
AE_DEATHSTAR_WORKTREE=${AE_DEATHSTAR_WORKTREE:-${AE_MAIN_DIR}/.ae-worktrees/deathstar}

ae_deathstar_die() {
  echo "[FAIL] $*" >&2
  exit 1
}

ae_prepare_deathstar_worktree() {
  echo "==> Preparing DeathStar source"
  git -C "${AE_MAIN_DIR}" fetch -q origin nsdi27-ae-deathstar ||
    ae_deathstar_die "could not fetch origin/nsdi27-ae-deathstar"
  git -C "${AE_MAIN_DIR}" cat-file -e "${AE_DEATHSTAR_COMMIT}^{commit}" ||
    ae_deathstar_die "pinned DeathStar commit is unavailable"

  mkdir -p "$(dirname "${AE_DEATHSTAR_WORKTREE}")"
  git -C "${AE_MAIN_DIR}" worktree prune

  if [ ! -e "${AE_DEATHSTAR_WORKTREE}/.git" ]; then
    if [ -e "${AE_DEATHSTAR_WORKTREE}" ]; then
      ae_deathstar_die \
        "${AE_DEATHSTAR_WORKTREE} exists but is not a Git worktree; move it aside"
    fi
    echo "==> Creating managed worktree: ${AE_DEATHSTAR_WORKTREE}"
    git -C "${AE_MAIN_DIR}" worktree add --detach \
      "${AE_DEATHSTAR_WORKTREE}" "${AE_DEATHSTAR_COMMIT}" ||
      ae_deathstar_die "could not create the DeathStar worktree"
  else
    if [ -n "$(git -C "${AE_DEATHSTAR_WORKTREE}" status --porcelain)" ]; then
      ae_deathstar_die \
        "managed worktree has local changes: ${AE_DEATHSTAR_WORKTREE}
       Move/commit them, then re-run."
    fi
    echo "==> Updating DeathStar source"
    git -C "${AE_DEATHSTAR_WORKTREE}" checkout -q --detach \
      "${AE_DEATHSTAR_COMMIT}" ||
      ae_deathstar_die "could not update the DeathStar worktree"
  fi

  echo "  [ OK ] DeathStar source ready"

  export LITELIB_MAIN_DIR="${AE_MAIN_DIR}"
  export LITELIB_WORKTREE_DIR="${AE_DEATHSTAR_WORKTREE}"
}

ae_run_deathstar_script() {
  local script=$1
  shift
  ae_prepare_deathstar_worktree
  local path="${AE_DEATHSTAR_WORKTREE}/tests/DeathStar/scripts/${script}"
  [ -x "${path}" ] || ae_deathstar_die "missing executable: ${path}"
  cd "$(dirname "${path}")"
  exec "${path}" "$@"
}
