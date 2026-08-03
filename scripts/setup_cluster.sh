#!/bin/bash
#
# One-shot cluster bring-up for the LiteLib artifact evaluation.
#
# Run this from node0 (as the regular, non-root CloudLab account).  It will, on
# every node of the cluster:
#
#   1. bootstrap SSH so that node-to-node and node-to-GitHub access works
#      without any interactive fingerprint / password prompt,
#   2. clone (or update) the LiteLib repository into the same path,
#   3. run scripts/init.sh unattended,
#   4. verify the result with scripts/check_init.sh.
#
# Everything is idempotent: re-running the script only redoes the work that is
# actually missing.
#
# Usage: ./setup_cluster.sh [OPTIONS] [COMMAND]
#
# Commands:
#   setup     (default) ssh bootstrap + clone + init + check on every node
#   ssh       only bootstrap SSH keys / known_hosts
#   clone     only clone or update the repository
#   init      only run init.sh (assumes the repository is already there)
#   check     only run check_init.sh and print a per-node report
#   reboot    reboot the nodes into the LiteLib kernel and wait for them
#
# Options:
#   -n, --nodes "n0 n1 ..."  nodes to operate on (default: $LITELIB_NODES)
#   -u, --user USER          SSH account (default: current user)
#   -r, --repo URL           repository URL to clone
#   -b, --branch BRANCH      branch to check out (default: master)
#   -d, --dir PATH           checkout path on every node (default: ~/LiteLib)
#   -f, --force              run init.sh even if the node already passes the check
#       --serial             one node at a time (default: all nodes in parallel)
#       --sync-local         rsync this working tree to the peers instead of
#                            cloning from GitHub (useful for local development)
#       --include-self       let `reboot` also reboot the node you are on
#       --skip-check         do not run check_init.sh after init.sh
#       --no-progress        do not print live per-node progress
#   -h, --help               show this help
#
# While a stage runs, the script prints what every node is currently doing and
# a heartbeat every couple of minutes.  The complete, unabridged output of each
# node is written to logs/<timestamp>-<stage>-<node>.log; follow one with
#
#   tail -f logs/*-init-node1.log
#
# Example:
#   ./setup_cluster.sh                       # full bring-up of all four nodes
#   ./setup_cluster.sh check                 # just re-verify the cluster
#   ./setup_cluster.sh -n "node1 node2" init # re-initialize two nodes

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

COMMAND=setup
FORCE=0
PARALLEL=1
SYNC_LOCAL=0
INCLUDE_SELF=0
SKIP_CHECK=0
SHOW_PROGRESS=${LITELIB_PROGRESS:-1}
LITELIB_POLL_SECS=${LITELIB_POLL_SECS:-5}
LITELIB_HEARTBEAT_SECS=${LITELIB_HEARTBEAT_SECS:-120}

while [ $# -gt 0 ]; do
  case "$1" in
  -n | --nodes)
    LITELIB_NODES=$2
    shift
    ;;
  -u | --user)
    LITELIB_SSH_USER=$2
    shift
    ;;
  -r | --repo)
    LITELIB_REPO_URL=$2
    shift
    ;;
  -b | --branch)
    LITELIB_REPO_BRANCH=$2
    shift
    ;;
  -d | --dir)
    LITELIB_REPO_DIR=$2
    shift
    ;;
  -f | --force) FORCE=1 ;;
  --serial) PARALLEL=0 ;;
  --sync-local) SYNC_LOCAL=1 ;;
  --include-self) INCLUDE_SELF=1 ;;
  --skip-check) SKIP_CHECK=1 ;;
  --no-progress) SHOW_PROGRESS=0 ;;
  -h | --help)
    sed -n '2,60p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  setup | ssh | clone | init | check | reboot) COMMAND=$1 ;;
  *)
    echo "unknown argument: $1 (try --help)" 1>&2
    exit 2
    ;;
  esac
  shift
done

read -r -a NODES <<<"${LITELIB_NODES}"
SELF=$(hostname -s)
LOG_DIR=${LITELIB_LOG_DIR:-${LITELIB_REPO_DIR}/logs}
RUN_ID=$(date +%Y%m%d-%H%M%S)
# shellcheck disable=SC2206
SSH_OPTS=(${LITELIB_SSH_OPTS})

mkdir -p "${LOG_DIR}"

C_INFO=$'\033[1;34m'
C_OK=$'\033[32m'
C_BAD=$'\033[31m'
C_WARN=$'\033[33m'
C_OFF=$'\033[0m'
if [ ! -t 1 ]; then C_INFO=""; C_OK=""; C_BAD=""; C_WARN=""; C_OFF=""; fi

info() { echo "${C_INFO}==>${C_OFF} $*"; }
ok() { echo "  ${C_OK}[ OK ]${C_OFF} $*"; }
warn() { echo "  ${C_WARN}[WARN]${C_OFF} $*"; }
err() { echo "  ${C_BAD}[FAIL]${C_OFF} $*" 1>&2; }
die() {
  err "$*"
  exit 1
}

# rsh <node> <command...>  -- run a command on a node (locally when it is us)
rsh() {
  local node=$1
  shift
  if [ "${node}" = "${SELF}" ]; then
    bash -c "$*"
  else
    ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" "$*"
  fi
}

# ---------------------------------------------------------------------------
# Parallel driver
# ---------------------------------------------------------------------------

fmt_elapsed() {
  local s=$1
  printf '%02d:%02d' $((s / 60)) $((s % 60))
}

# node_phase <stage> <node> -- last phase announced by that node, if any.
node_phase() {
  local log="${LOG_DIR}/${RUN_ID}-$1-$2.log"
  [ -f "${log}" ] || return 0
  # Anchor on '^' so the `set -x` trace of the echo is not matched.
  grep -a "^${LITELIB_PHASE_MARKER} " "${log}" 2>/dev/null | tail -1 |
    sed "s|^${LITELIB_PHASE_MARKER} ||"
}

# monitor_progress <stage> <node...>
# Streams a live view of what each node is doing.  Nodes signal completion by
# creating a .done stamp, so the monitor needs no knowledge of the job PIDs.
monitor_progress() {
  local stage=$1
  shift
  local nodes=("$@")
  local start=${SECONDS} last_beat=${SECONDS}
  local -A shown=()
  local node phase running busy

  while :; do
    running=0
    busy=()
    for node in "${nodes[@]}"; do
      phase=$(node_phase "${stage}" "${node}")
      if [ ! -f "${LOG_DIR}/.${RUN_ID}-${stage}-${node}.done" ]; then
        running=$((running + 1))
        busy+=("${node}${phase:+: ${phase}}")
      fi
      if [ -n "${phase}" ] && [ "${shown[${node}]:-}" != "${phase}" ]; then
        printf '  %s[%s]%s [%s] %s\n' \
          "${C_INFO}" "$(fmt_elapsed $((SECONDS - start)))" "${C_OFF}" "${node}" "${phase}"
        shown[${node}]=${phase}
      fi
    done
    [ "${running}" -eq 0 ] && break
    # Heartbeat, so a long silent step still shows the run is alive.
    if [ $((SECONDS - last_beat)) -ge "${LITELIB_HEARTBEAT_SECS}" ]; then
      printf '  %s[%s]%s still running (%d/%d): %s\n' \
        "${C_INFO}" "$(fmt_elapsed $((SECONDS - start)))" "${C_OFF}" \
        "${running}" "${#nodes[@]}" "$(
          IFS='; '
          echo "${busy[*]}"
        )"
      last_beat=${SECONDS}
    fi
    sleep "${LITELIB_POLL_SECS}"
  done
}

# for_each_node <stage> <function>
# Runs <function> <node> for every node, streaming its output into
# ${LOG_DIR}/<run>-<stage>-<node>.log, and fails if any node fails.
for_each_node() {
  local stage=$1 fn=$2
  local node pids=() nodes=() rc=0 monitor_pid=""

  for node in "${NODES[@]}"; do
    local log="${LOG_DIR}/${RUN_ID}-${stage}-${node}.log"
    local stamp="${LOG_DIR}/.${RUN_ID}-${stage}-${node}.done"
    rm -f "${stamp}"
    if [ "${PARALLEL}" -eq 1 ]; then
      (
        "${fn}" "${node}" >"${log}" 2>&1
        echo $? >"${stamp}"
      ) &
      pids+=("$!")
      nodes+=("${node}")
    else
      info "[${node}] ${stage} (following ${log})"
      if "${fn}" "${node}" 2>&1 | tee "${log}"; then
        ok "[${node}] ${stage}"
      else
        err "[${node}] ${stage} failed, see ${log}"
        rc=1
      fi
      : >"${stamp}"
    fi
  done

  if [ "${PARALLEL}" -eq 1 ]; then
    info "${stage}: running on ${#NODES[@]} nodes in parallel"
    info "full output: ${LOG_DIR}/${RUN_ID}-${stage}-<node>.log"
    if [ "${SHOW_PROGRESS}" -eq 1 ]; then
      monitor_progress "${stage}" "${nodes[@]}" &
      monitor_pid=$!
    fi
    local i
    for i in "${!pids[@]}"; do
      if wait "${pids[$i]}"; then
        :
      else
        rc=1
      fi
    done
    [ -n "${monitor_pid}" ] && wait "${monitor_pid}" 2>/dev/null
    for i in "${!nodes[@]}"; do
      local stamp="${LOG_DIR}/.${RUN_ID}-${stage}-${nodes[$i]}.done"
      if [ "$(cat "${stamp}" 2>/dev/null)" = "0" ]; then
        ok "[${nodes[$i]}] ${stage}"
      else
        err "[${nodes[$i]}] ${stage} failed, see ${LOG_DIR}/${RUN_ID}-${stage}-${nodes[$i]}.log"
        tail -n 25 "${LOG_DIR}/${RUN_ID}-${stage}-${nodes[$i]}.log" 1>&2
        rc=1
      fi
    done
  fi
  return "${rc}"
}

# ---------------------------------------------------------------------------
# Stage: SSH bootstrap
# ---------------------------------------------------------------------------

known_hosts_snippet() {
  # Emit a shell snippet that adds the given hosts to ~/.ssh/known_hosts if the
  # key is not recorded yet.  This is what removes the interactive
  # "Are you sure you want to continue connecting (yes/no)?" prompt.
  cat <<'EOS'
SSH_DIR="$(getent passwd "$(id -un)" | cut -d: -f6)/.ssh"
mkdir -p "$SSH_DIR" && chmod 700 "$SSH_DIR"
touch "$SSH_DIR/known_hosts" && chmod 644 "$SSH_DIR/known_hosts"
for h in HOSTS_PLACEHOLDER; do
  if ! ssh-keygen -f "$SSH_DIR/known_hosts" -F "$h" >/dev/null 2>&1; then
    ssh-keyscan -T 10 -t rsa,ecdsa,ed25519 "$h" >>"$SSH_DIR/known_hosts" 2>/dev/null
  fi
done
EOS
}

bootstrap_local_ssh() {
  local key="${LITELIB_SSH_KEY}"
  if [ ! -f "${key}" ]; then
    info "generating SSH keypair ${key}"
    mkdir -p "$(dirname "${key}")"
    chmod 700 "$(dirname "${key}")"
    ssh-keygen -t ed25519 -N "" -f "${key}" -C "litelib-ae@$(hostname -s)" >/dev/null
  fi
  # CloudLab installs the private key without its .pub counterpart; derive it
  # rather than generating a brand-new (and therefore unauthorized) keypair.
  if [ ! -s "${key}.pub" ]; then
    info "deriving public key ${key}.pub from the private key"
    ssh-keygen -y -f "${key}" >"${key}.pub" ||
      die "cannot read ${key} (is it passphrase-protected?)"
    chmod 644 "${key}.pub"
  fi
  # Trust ourselves so that `ssh node0` works like any other node.
  touch ~/.ssh/authorized_keys
  chmod 600 ~/.ssh/authorized_keys
  # ssh-keygen -y omits the comment, so match on the key material only.
  local keytype keydata
  read -r keytype keydata _ <"${key}.pub"
  grep -qF "${keydata}" ~/.ssh/authorized_keys ||
    printf '%s %s litelib-ae\n' "${keytype}" "${keydata}" >>~/.ssh/authorized_keys

  local hosts="github.com ${LITELIB_NODES}"
  known_hosts_snippet | sed "s|HOSTS_PLACEHOLDER|${hosts}|" | bash
  ok "local SSH bootstrap done (${key})"
}

stage_ssh() {
  bootstrap_local_ssh

  local keytype keydata
  read -r keytype keydata _ <"${LITELIB_SSH_KEY}.pub"
  local pub="${keytype} ${keydata} litelib-ae"
  local node
  for node in "${NODES[@]}"; do
    [ "${node}" = "${SELF}" ] && continue
    info "[${node}] authorizing key and seeding known_hosts"
    # ssh-copy-id needs a password prompt; on CloudLab the account is already
    # authorized cluster-wide, so a plain BatchMode ssh is enough.  If it is
    # not, tell the user exactly what to do instead of hanging on a prompt.
    if ! ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" true 2>/dev/null; then
      die "cannot SSH to ${LITELIB_SSH_USER}@${node} without a password.
     Run 'ssh-copy-id ${LITELIB_SSH_USER}@${node}' once, then re-run this script."
    fi
    ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" \
      "mkdir -p ~/.ssh && chmod 700 ~/.ssh && touch ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys && grep -qF '${keydata}' ~/.ssh/authorized_keys || echo '${pub}' >> ~/.ssh/authorized_keys"
    # The experiment scripts SSH *between* nodes, so every node needs the key.
    if ! ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" "test -s ~/.ssh/$(basename "${LITELIB_SSH_KEY}").pub"; then
      scp "${SSH_OPTS[@]}" -q "${LITELIB_SSH_KEY}" "${LITELIB_SSH_KEY}.pub" \
        "${LITELIB_SSH_USER}@${node}:~/.ssh/"
      ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" \
        "chmod 600 ~/.ssh/$(basename "${LITELIB_SSH_KEY}")"
    fi
    known_hosts_snippet | sed "s|HOSTS_PLACEHOLDER|github.com ${LITELIB_NODES}|" |
      ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" bash
    # root runs network_limit.sh, which SSHes to the peers as well.  `sudo -H`
    # is required, otherwise HOME still points at the unprivileged account and
    # the keys land in the wrong known_hosts file.
    known_hosts_snippet | sed "s|HOSTS_PLACEHOLDER|github.com ${LITELIB_NODES}|" |
      ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" sudo -n -H bash
    ok "[${node}] SSH ready"
  done
  known_hosts_snippet | sed "s|HOSTS_PLACEHOLDER|github.com ${LITELIB_NODES}|" | sudo -n -H bash
  ok "[${SELF}] SSH ready"
}

# ---------------------------------------------------------------------------
# Stage: clone / update the repository
# ---------------------------------------------------------------------------

resolve_repo_url() {
  # Prefer the configured URL; fall back to HTTPS when the SSH remote is not
  # reachable (e.g. the evaluator has no GitHub key on the node).
  if GIT_SSH_COMMAND="ssh ${LITELIB_SSH_OPTS}" GIT_TERMINAL_PROMPT=0 \
    git ls-remote "${LITELIB_REPO_URL}" HEAD >/dev/null 2>&1; then
    echo "${LITELIB_REPO_URL}"
    return 0
  fi
  if GIT_TERMINAL_PROMPT=0 git ls-remote "${LITELIB_REPO_HTTPS_URL}" HEAD >/dev/null 2>&1; then
    echo "${LITELIB_REPO_HTTPS_URL}"
    return 0
  fi
  return 1
}

clone_node() {
  local node=$1
  local url=$2
  rsh "${node}" "
    set -e
    export GIT_TERMINAL_PROMPT=0
    export GIT_SSH_COMMAND='ssh ${LITELIB_SSH_OPTS}'
    if [ ! -d '${LITELIB_REPO_DIR}/.git' ]; then
      echo '[clone] ${url} -> ${LITELIB_REPO_DIR}'
      git clone --branch '${LITELIB_REPO_BRANCH}' '${url}' '${LITELIB_REPO_DIR}'
    else
      echo '[update] ${LITELIB_REPO_DIR}'
      cd '${LITELIB_REPO_DIR}'
      git remote set-url origin '${url}'
      git fetch --prune origin
      if [ -z \"\$(git status --porcelain --untracked-files=no)\" ]; then
        git checkout '${LITELIB_REPO_BRANCH}'
        git merge --ff-only 'origin/${LITELIB_REPO_BRANCH}' || \
          echo '[update] cannot fast-forward, keeping the current commit'
      else
        echo '[update] working tree has local changes, not touching it'
      fi
    fi
    cd '${LITELIB_REPO_DIR}'
    git submodule update --init --recursive || \
      echo '[warn] submodule checkout failed (needed only by some workloads)'
    git --no-pager log -1 --oneline
  "
}

sync_node() {
  local node=$1
  if [ "${node}" = "${SELF}" ]; then
    echo "[sync] ${node} is the source, nothing to do"
    return 0
  fi
  echo "[sync] ${SCRIPT_DIR%/scripts} -> ${node}:${LITELIB_REPO_DIR}"
  rsh "${node}" "mkdir -p '${LITELIB_REPO_DIR}'"
  rsync -az --delete --exclude '.git' --exclude 'logs' --exclude 'build' \
    -e "ssh ${LITELIB_SSH_OPTS}" \
    "${SCRIPT_DIR%/scripts}/" "${LITELIB_SSH_USER}@${node}:${LITELIB_REPO_DIR}/"
}

stage_clone() {
  if [ "${SYNC_LOCAL}" -eq 1 ]; then
    for_each_node sync sync_node
    return
  fi
  info "resolving repository URL"
  local url
  if ! url=$(resolve_repo_url); then
    die "cannot reach ${LITELIB_REPO_URL} nor ${LITELIB_REPO_HTTPS_URL}.
     Add an SSH key to your GitHub account (and to every node), or pass
     --repo <url> with a reachable URL."
  fi
  ok "using ${url}"
  # shellcheck disable=SC2317
  _clone_node() { clone_node "$1" "${url}"; }
  for_each_node clone _clone_node
}

# ---------------------------------------------------------------------------
# Stage: run init.sh
# ---------------------------------------------------------------------------

init_node() {
  local node=$1
  rsh "${node}" "
    set -e
    test -x '${LITELIB_REPO_DIR}/scripts/init.sh' || {
      echo 'scripts/init.sh not found in ${LITELIB_REPO_DIR}; run the clone stage first' >&2
      exit 1
    }
    sudo -n true 2>/dev/null || {
      echo 'passwordless sudo is required on every node' >&2
      exit 1
    }
    if [ '${FORCE}' -eq 0 ] && sudo -n '${LITELIB_REPO_DIR}/scripts/check_init.sh' --quiet; then
      echo '[init] already initialized, skipping (use --force to re-run)'
      exit 0
    fi
    cd '${LITELIB_REPO_DIR}/scripts'
    sudo -n --preserve-env=DEBIAN_FRONTEND,NEEDRESTART_MODE,NEEDRESTART_SUSPEND \
      DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a NEEDRESTART_SUSPEND=1 \
      ./init.sh </dev/null
  "
}

stage_init() {
  info "running init.sh (this takes ~20-40 min per node; nodes run concurrently)"
  for_each_node init init_node
}

# ---------------------------------------------------------------------------
# Stage: verify
# ---------------------------------------------------------------------------

stage_check() {
  local node rc=0 failed=()
  for node in "${NODES[@]}"; do
    echo
    if rsh "${node}" "sudo -n '${LITELIB_REPO_DIR}/scripts/check_init.sh'"; then
      :
    else
      failed+=("${node}")
      rc=1
    fi
  done
  echo
  if [ "${rc}" -eq 0 ]; then
    ok "all ${#NODES[@]} node(s) are initialized"
  else
    err "not initialized: ${failed[*]}"
    echo "     Re-run: $0 -n \"${failed[*]}\" init" 1>&2
  fi
  return "${rc}"
}

# ---------------------------------------------------------------------------
# Stage: reboot into the LiteLib kernel
# ---------------------------------------------------------------------------

wait_for_node() {
  local node=$1 deadline=$((SECONDS + 600))
  sleep 15
  while [ "${SECONDS}" -lt "${deadline}" ]; do
    if ssh "${SSH_OPTS[@]}" -o ConnectTimeout=5 "${LITELIB_SSH_USER}@${node}" true 2>/dev/null; then
      return 0
    fi
    sleep 5
  done
  return 1
}

stage_reboot() {
  local node rc=0
  for node in "${NODES[@]}"; do
    if [ "${node}" = "${SELF}" ]; then continue; fi
    info "[${node}] rebooting"
    ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" "sudo -n systemctl reboot" 2>/dev/null || true
    if wait_for_node "${node}"; then
      ok "[${node}] back up, running $(ssh "${SSH_OPTS[@]}" "${LITELIB_SSH_USER}@${node}" uname -r)"
    else
      err "[${node}] did not come back within 10 minutes"
      rc=1
    fi
  done

  # tc/iptables/cpufreq settings do not survive a reboot: re-apply them.
  info "re-applying runtime configuration after reboot"
  for_each_node reinit init_node || rc=1

  if [ "${INCLUDE_SELF}" -eq 1 ]; then
    warn "rebooting ${SELF} now -- this session will drop."
    warn "After it comes back, run: ${LITELIB_REPO_DIR}/scripts/setup_cluster.sh init"
    sleep 5
    sudo -n systemctl reboot
  elif [ "$(uname -r)" != "${LITELIB_KERNEL_RELEASE}" ]; then
    warn "${SELF} still runs $(uname -r); it is the node you are logged into, so it"
    warn "was not rebooted.  Reboot it yourself and re-run '$0 init' afterwards:"
    warn "    sudo systemctl reboot"
  fi
  return "${rc}"
}

# ---------------------------------------------------------------------------

info "cluster: ${LITELIB_NODES}"
info "user:    ${LITELIB_SSH_USER}"
info "repo:    ${LITELIB_REPO_URL} (${LITELIB_REPO_BRANCH}) -> ${LITELIB_REPO_DIR}"
info "command: ${COMMAND}"

case "${COMMAND}" in
ssh) stage_ssh ;;
clone)
  stage_ssh
  stage_clone
  ;;
init) stage_init ;;
check) stage_check ;;
reboot) stage_reboot ;;
setup)
  stage_ssh
  stage_clone
  stage_init
  if [ "${SKIP_CHECK}" -eq 0 ]; then
    stage_check || {
      err "post-init verification failed"
      exit 1
    }
  fi
  if [ "$(uname -r)" != "${LITELIB_KERNEL_RELEASE}" ]; then
    echo
    warn "init.sh installed kernel ${LITELIB_KERNEL_RELEASE} but the nodes still run"
    warn "an older kernel.  Reboot the cluster to activate it:"
    warn "    $0 reboot"
  fi
  ;;
esac
