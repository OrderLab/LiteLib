#!/bin/bash
#
# Figure 1 & 2 (Section 2, Motivation) -- STEP 1 of 3: prepare the environment.
#
# Brings the four-node cluster from "LiteLib base setup done" to "ready to run
# the DeathStarBench Social Network motivation experiment".  Every step is
# idempotent, so it is safe to re-run after a partial failure.
#
# Stages:
#   deps     install Docker + wrk2 build deps on every node
#   sync     put this branch's checkout at the same path on every node
#   build    build vanilla memcached, patch it, build LiteMemcached and wrk2
#   images   build the Docker images (lite-memcached, mcrouter, social network)
#   swarm    initialise the Docker Swarm across the four nodes
#   deploy   deploy the socialnetwork stack
#   prefill  populate MongoDB with the social graph and compose posts
#
# Usage:
#   ./ae_motivation_setup.sh [STAGE ...]      # default: all stages, in order
#
# Examples:
#   ./ae_motivation_setup.sh                  # full setup (~40-60 min)
#   ./ae_motivation_setup.sh build images     # rebuild after a code change
#   ./ae_motivation_setup.sh prefill          # just repopulate the database
#
# Node roles (fixed by the compose file):
#   node0  MongoDB (post storage)
#   node1  Swarm manager, nginx front end, workload generator target
#   node2  social network microservices
#   node3  Memcached x2 + Mcrouter, and where the experiment driver runs

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_common.sh
source "${SCRIPT_DIR}/ae_common.sh"

DEATHSTAR_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOCIAL_DIR="${DEATHSTAR_DIR}/src/socialNetwork"
MEMCACHED_SRC="${LITELIB_WORKTREE_DIR}/tests/Memcached/src"

# Where this checkout must live on every node.  The compose file bind-mounts the
# repository root into the containers as /workspace, so the path has to be
# identical everywhere.
REMOTE_DIR=${AE_REMOTE_DIR:-${LITELIB_WORKTREE_DIR}}

MEMCACHED_VERSION=${AE_MEMCACHED_VERSION:-1.6.14}
MEMCACHED_TARBALL="memcached-${MEMCACHED_VERSION}.tar.gz"
MEMCACHED_URL="https://memcached.org/files/${MEMCACHED_TARBALL}"
NUM_JOBS=${AE_NUM_JOBS:-$(nproc)}

case "${1:-}" in
-h | --help)
  sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit 0
  ;;
esac

STAGES=("$@")
if [ ${#STAGES[@]} -eq 0 ]; then
  STAGES=(deps sync build images swarm deploy prefill)
fi

read -r -a NODES <<<"${AE_NODES}"
RUN_LOG="${AE_LOGS_DIR}/motivation-setup-$(ae_run_id).log"

ae_info "cluster:  ${AE_NODES}"
ae_info "checkout: ${REMOTE_DIR} (must be identical on every node)"
ae_info "log:      ${RUN_LOG}"

# ---------------------------------------------------------------------------
# deps -- Docker and the packages the build needs, on every node
# ---------------------------------------------------------------------------

stage_deps() {
  ae_info "installing Docker and build dependencies on every node"
  local node pids=()
  for node in "${NODES[@]}"; do
    (
      # tests/DeathStar/scripts/init.sh installs Docker, the compose plugin and
      # the Lua bits wrk2 needs, and adds the invoking account to the docker
      # group.  It is fed over stdin so the node does not need the repo yet.
      ssh ${AE_SSH_OPTS} "${node}" \
        "sudo -n DEBIAN_FRONTEND=noninteractive SUDO_USER=\$(id -un) bash -s" \
        <"${SCRIPT_DIR}/init.sh" >"${AE_LOGS_DIR}/motivation-deps-${node}.log" 2>&1
      echo "$?" >"${AE_LOGS_DIR}/.deps-${node}.rc"
    ) &
    pids+=("$!")
  done
  for p in "${pids[@]}"; do wait "${p}" || true; done

  local rc=0
  for node in "${NODES[@]}"; do
    if [ "$(cat "${AE_LOGS_DIR}/.deps-${node}.rc" 2>/dev/null)" = "0" ]; then
      ae_ok "[${node}] dependencies installed"
    else
      ae_err "[${node}] failed, see ${AE_LOGS_DIR}/motivation-deps-${node}.log"
      rc=1
    fi
    rm -f "${AE_LOGS_DIR}/.deps-${node}.rc"
  done
  [ "${rc}" -eq 0 ] || return 1

  # Group membership only takes effect on a new session; make docker usable now
  # instead of asking the evaluator to log out and back in.
  for node in "${NODES[@]}"; do
    ssh ${AE_SSH_OPTS} "${node}" "docker info >/dev/null 2>&1" ||
      ae_warn "[${node}] docker needs sudo for this session; the scripts use 'sg docker' where required"
  done
}

# ---------------------------------------------------------------------------
# sync -- identical checkout on every node
# ---------------------------------------------------------------------------

stage_sync() {
  ae_info "syncing ${LITELIB_WORKTREE_DIR} to every node at ${REMOTE_DIR}"
  local node
  for node in "${NODES[@]}"; do
    if [ "${node}" = "$(hostname -s)" ]; then
      ae_ok "[${node}] source node"
      continue
    fi
    ssh ${AE_SSH_OPTS} "${node}" "mkdir -p '${REMOTE_DIR}'"
    # --delete keeps the nodes byte-identical; .git is excluded because a
    # linked worktree's .git points into the main checkout, which only exists
    # on this node.
    #
    # Build outputs are excluded as well.  They are produced *on* each node by
    # the build stage and do not exist here, so without these exclusions
    # --delete would remove the compiled wrk2, memcached and LiteMemcached
    # binaries from the nodes every time the tree is re-synced.
    rsync -az --delete \
      --exclude '.git' --exclude 'results' --exclude 'figures' \
      --exclude 'logs' --exclude '.venv' \
      --exclude 'build/' \
      --exclude 'tests/Memcached/src/memcached/' \
      --exclude 'tests/Memcached/src/memcached-vanilla' \
      --exclude 'tests/Memcached/src/memcached-*.tar.gz' \
      --exclude 'tests/DeathStar/src/wrk2/wrk' \
      --exclude 'tests/DeathStar/src/wrk2/obj/' \
      --exclude 'tests/DeathStar/src/wrk2/deps/' \
      -e "ssh ${AE_SSH_OPTS}" \
      "${LITELIB_WORKTREE_DIR}/" "${node}:${REMOTE_DIR}/" ||
      ae_die "rsync to ${node} failed"
    ae_ok "[${node}] synced"
  done
}

# ---------------------------------------------------------------------------
# build -- memcached (vanilla + patched), LiteMemcached, wrk2
# ---------------------------------------------------------------------------

# The embedded LiteMemcached build has to happen first: the patched memcached
# links against libembedded_lite_memcached.so from its build directory.
build_litememcached() {
  ae_info "building LiteMemcached (lite-version-ascii-embedded)"
  ae_rsh node3 "
    set -e
    cd '${MEMCACHED_SRC}/lite-version-ascii-embedded'
    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release .. >/dev/null
    make -j${NUM_JOBS}
    ls -l LiteMemcached libembedded_lite_memcached.so
  "
}

# Vanilla memcached first, then the LiteLib patch on top.  The unpatched binary
# is kept because the 'vanilla' arm of the experiment runs it as-is; the patch
# turns the same tree into the embedded LiteLib build.
build_memcached() {
  ae_info "building memcached ${MEMCACHED_VERSION} (vanilla, then patched)"
  ae_rsh node3 "
    set -e
    cd '${MEMCACHED_SRC}'

    # 1. fetch and unpack the pristine release
    if [ ! -d memcached/.pristine ]; then
      rm -rf memcached
      mkdir -p memcached
      cd '${MEMCACHED_SRC}'
      [ -f '${MEMCACHED_TARBALL}' ] || wget -q '${MEMCACHED_URL}'
      tar -xzf '${MEMCACHED_TARBALL}' --strip-components=1 -C memcached
      touch memcached/.pristine
    fi

    cd '${MEMCACHED_SRC}/memcached'

    # 2. build the *unpatched* binary and keep a copy.  The vanilla arm of the
    #    experiment uses the distro memcached, but having this proves the tree
    #    builds cleanly before the patch is applied.
    if [ ! -f ../memcached-vanilla ]; then
      ./configure --quiet >/dev/null
      make -j${NUM_JOBS} memcached >/dev/null
      cp memcached ../memcached-vanilla
      make distclean >/dev/null 2>&1 || true
    fi

    # 3. apply the LiteLib patch (idempotent)
    if [ ! -f .litesys-patched ]; then
      patch -p1 --forward < '${MEMCACHED_SRC}/memcached.1.6.14.patch'
      touch .litesys-patched
    fi

    # 4. the patch adds vendor/LiteSys symlinks into the LiteMemcached build
    mkdir -p vendor/LiteSys
    ln -sfn '${MEMCACHED_SRC}/lite-version-ascii-embedded/build' vendor/LiteSys/build
    ln -sfn '${MEMCACHED_SRC}/lite-version-ascii-embedded/Lite/include/embedded_lite.h' vendor/LiteSys/embedded_lite.h

    # 5. build the patched (LiteLib-enabled) memcached
    ./autogen.sh >/dev/null 2>&1 || autoreconf -i >/dev/null 2>&1 || true
    ./configure --quiet >/dev/null
    make -j${NUM_JOBS} memcached
    ls -l memcached ../memcached-vanilla
  "
}

build_wrk2() {
  # wrk2 is needed on node1 (database prefill / post composition) and on node3
  # (the experiment's workload generator).
  local wrk_nodes=${AE_WRK_NODES:-"node1 node3"}
  ae_info "building the wrk2 workload generator on: ${wrk_nodes}"
  # wrk2 vendors LuaJIT as a git submodule.  A fresh clone leaves it empty, and
  # the build then fails with "deps/luajit/src: No such file or directory".
  # Initialise it here (over HTTPS, so no GitHub account is needed) and sync the
  # result out, because the worker nodes hold a plain copy with no .git.
  local luajit_rel="tests/DeathStar/src/wrk2/deps/luajit"
  local luajit_dir="${LITELIB_WORKTREE_DIR}/${luajit_rel}"
  if [ ! -f "${luajit_dir}/Makefile" ]; then
    ae_info "fetching the LuaJIT submodule"
    local url commit
    url=$(git -C "${LITELIB_WORKTREE_DIR}" config -f .gitmodules --get-regexp 'submodule\..*luajit\.url' | awk '{print $2}' | head -1)
    url=${url:-https://github.com/LuaJIT/LuaJIT.git}
    url=${url/git@github.com:/https://github.com/}
    commit=$(git -C "${LITELIB_WORKTREE_DIR}" ls-tree HEAD "${luajit_rel}" | awk '{print $3}')
    git -C "${LITELIB_WORKTREE_DIR}" submodule update --init "${luajit_rel}" 2>/dev/null || {
      ae_warn "submodule update failed, cloning ${url} directly"
      rm -rf "${luajit_dir}"
      git clone -q "${url}" "${luajit_dir}" || return 1
      [ -n "${commit}" ] && git -C "${luajit_dir}" checkout -q "${commit}"
    }
    [ -f "${luajit_dir}/Makefile" ] || {
      ae_err "could not obtain LuaJIT into ${luajit_dir}"
      return 1
    }
  fi

  local node
  for node in ${wrk_nodes}; do
    if [ "${node}" != "$(hostname -s)" ]; then
      rsync -az --exclude '.git' -e "ssh ${AE_SSH_OPTS}" \
        "${luajit_dir}/" "${node}:${luajit_dir}/" || return 1
    fi
    ae_rsh "${node}" "
      set -e
      cd '${DEATHSTAR_DIR}/src/wrk2'
      [ -x ./wrk ] || make -j${NUM_JOBS}
      ls -l ./wrk
    " || return 1
    ae_ok "[${node}] wrk2 built"
  done
}

stage_build() {
  build_litememcached || return 1
  build_memcached || return 1
  build_wrk2 || return 1
}

# ---------------------------------------------------------------------------
# images / swarm / deploy -- reuse the repository's own helpers
# ---------------------------------------------------------------------------

stage_images() {
  ae_info "building the Docker images (lite-memcached, mcrouter, social network, mongo)"
  "${SCRIPT_DIR}/swarm_helper_replica.sh" build
}

stage_swarm() {
  ae_info "initialising the Docker Swarm (manager: node1)"
  # swarm_init.sh must run on the manager.
  ae_rsh node1 "cd '${DEATHSTAR_DIR}/scripts' && ./swarm_init.sh"
}

stage_deploy() {
  ae_info "deploying the socialnetwork stack"
  ae_rsh node1 "cd '${DEATHSTAR_DIR}/scripts' && ./swarm_helper_replica.sh up"
  ae_info "waiting for the services to converge"
  local i
  for i in $(seq 1 60); do
    if ae_rsh node1 "curl -sf -o /dev/null http://node1:8080/wrk2-api/home-timeline/read?user_id=1'&'start=0'&'stop=1" 2>/dev/null; then
      ae_ok "front end is answering"
      return 0
    fi
    sleep 10
  done
  ae_warn "front end did not answer within 10 minutes; check 'docker stack services socialnetwork' on node1"
}

# ---------------------------------------------------------------------------
# prefill -- populate the database (social graph + posts)
# ---------------------------------------------------------------------------

stage_prefill() {
  ae_info "populating MongoDB with the social graph (socfb-Reed98)"
  ae_rsh node1 "
    set -e
    cd '${SOCIAL_DIR}'
    python3 scripts/init_social_graph.py --graph=socfb-Reed98 --ip node1 --compose
  " || ae_die "social graph initialisation failed"
  ae_ok "social graph loaded"

  ae_info "composing posts to fill the cache (10 min at 2000 req/s)"
  ae_rsh node1 "
    set -e
    cd '${SOCIAL_DIR}'
    ../wrk2/wrk -D exp -t 40 -c 40 -d 600 -L \
      -s ./wrk2/scripts/social-network/compose-post.lua \
      http://node1:8080/wrk2-api/post/compose -R 2000
  " || ae_die "post composition failed"
  ae_ok "database prefilled"
}

# ---------------------------------------------------------------------------

main() {
  local stage rc=0
  for stage in "${STAGES[@]}"; do
    echo
    ae_info "=== stage: ${stage} ==="
    case "${stage}" in
    deps) stage_deps || rc=1 ;;
    sync) stage_sync || rc=1 ;;
    build) stage_build || rc=1 ;;
    images) stage_images || rc=1 ;;
    swarm) stage_swarm || rc=1 ;;
    deploy) stage_deploy || rc=1 ;;
    prefill) stage_prefill || rc=1 ;;
    *) ae_die "unknown stage: ${stage}" ;;
    esac
    [ "${rc}" -eq 0 ] || ae_die "stage '${stage}' failed (see ${RUN_LOG})"
    ae_ok "stage '${stage}' complete"
  done

  echo
  ae_ok "environment ready. Next: ./ae_motivation_run.sh"
}

main 2>&1 | tee -a "${RUN_LOG}"
exit "${PIPESTATUS[0]}"
