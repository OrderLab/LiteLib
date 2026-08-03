#!/bin/bash
#
# Shared configuration for all LiteLib cluster setup scripts.
#
# This file is *sourced*, never executed.  Every value can be overridden by
# exporting the corresponding environment variable before invoking any script,
# e.g.
#
#   LITELIB_NODES="node0 node1" ./setup_cluster.sh
#
# The defaults describe the reference artifact-evaluation testbed: a 4-node
# CloudLab `c220g5` cluster running Ubuntu 22.04.  Because every node of that
# profile is provisioned identically, the hardware values below are assumed to
# be correct rather than auto-detected.

# --- Cluster topology --------------------------------------------------------

# Nodes taking part in the experiments.  node0 is the control node from which
# setup_cluster.sh is launched.
LITELIB_NODES=${LITELIB_NODES:-"node0 node1 node2 node3"}

# Account used for inter-node SSH.  When a script runs under `sudo`, $SUDO_USER
# holds the unprivileged account that invoked it.
LITELIB_SSH_USER=${LITELIB_SSH_USER:-${SUDO_USER:-$(id -un)}}
LITELIB_USER_HOME=${LITELIB_USER_HOME:-$(getent passwd "$LITELIB_SSH_USER" | cut -d: -f6)}
LITELIB_SSH_KEY=${LITELIB_SSH_KEY:-${LITELIB_USER_HOME}/.ssh/id_ed25519}

# --- Repository --------------------------------------------------------------

LITELIB_REPO_URL=${LITELIB_REPO_URL:-git@github.com:OrderLab/LiteLib.git}
LITELIB_REPO_HTTPS_URL=${LITELIB_REPO_HTTPS_URL:-https://github.com/OrderLab/LiteLib.git}
LITELIB_REPO_BRANCH=${LITELIB_REPO_BRANCH:-master}
LITELIB_REPO_DIR=${LITELIB_REPO_DIR:-${LITELIB_USER_HOME}/LiteLib}

# --- Hardware (CloudLab c220g5 defaults) -------------------------------------

# Shared, rate-limited control network (routable, used for apt/git traffic).
LITELIB_CTRL_IFACE=${LITELIB_CTRL_IFACE:-eno1}
# Dedicated 10GbE experiment network (10.10.1.0/24, used by the workloads).
LITELIB_EXP_IFACE=${LITELIB_EXP_IFACE:-enp94s0f0}

# Boot disk and root partition.  These are auto-detected from the live mount of
# `/` rather than hardcoded: identically provisioned c220g5 nodes do *not*
# necessarily enumerate their disks in the same order, so the root filesystem
# may sit on /dev/sda3 on one node and /dev/sdb3 on the next.
litelib_detect_root_device() {
  local src part disk
  src=$(findmnt -no SOURCE / 2>/dev/null) || return 1
  # /etc/fstab may reference the device by UUID or label.
  src=$(realpath "${src}" 2>/dev/null || echo "${src}")
  part=${src#/dev/}
  [ -r "/sys/class/block/${part}/partition" ] || return 1
  disk=$(basename "$(readlink -f "/sys/class/block/${part}/..")")
  printf '/dev/%s %s\n' "${disk}" "$(cat "/sys/class/block/${part}/partition")"
}

if [ -z "${LITELIB_ROOT_DISK:-}" ] || [ -z "${LITELIB_ROOT_PART:-}" ]; then
  _litelib_root=$(litelib_detect_root_device || true)
  LITELIB_ROOT_DISK=${LITELIB_ROOT_DISK:-${_litelib_root%% *}}
  LITELIB_ROOT_PART=${LITELIB_ROOT_PART:-${_litelib_root##* }}
  unset _litelib_root
fi

# Root filesystem size (GiB) below which we consider the disk *not* resized.
LITELIB_MIN_ROOT_GIB=${LITELIB_MIN_ROOT_GIB:-100}
# Pinned CPU frequency (GHz).  c220g5 nodes use Xeon Silver 4114 @ 2.20GHz.
LITELIB_CPU_FREQ_GHZ=${LITELIB_CPU_FREQ_GHZ:-2.2}

# --- Control-network rate limits (see network_limit.sh) ----------------------

LITELIB_PKTS_PER_SEC=${LITELIB_PKTS_PER_SEC:-10000}
LITELIB_RATE_LIMIT=${LITELIB_RATE_LIMIT:-100mbit}

# --- Software versions -------------------------------------------------------

LITELIB_KERNEL_RELEASE=${LITELIB_KERNEL_RELEASE:-6.8.0-52-generic}
LITELIB_KERNEL_PKG_VERSION=${LITELIB_KERNEL_PKG_VERSION:-6.8.0-52.53~22.04.1}
LITELIB_BOOST_VERSION=${LITELIB_BOOST_VERSION:-1.87.0}
LITELIB_LIBEVENT_VERSION=${LITELIB_LIBEVENT_VERSION:-2.1.12}
LITELIB_PREFIX=${LITELIB_PREFIX:-/usr/local}
LITELIB_NUM_JOBS=${LITELIB_NUM_JOBS:-$(nproc)}

# Distro packages required on every node.  Kept here (instead of inline in
# litesys_dependency.sh) so that check_init.sh verifies exactly what gets
# installed.  Kernel and kernel-tools packages are handled separately because
# their names embed the kernel release.
LITELIB_APT_PACKAGES=(
  build-essential
  nuttcp
  software-properties-common
  autoconf
  automake
  libtool
  pkg-config
  ca-certificates
  libssl-dev
  python3
  python3-dev
  python3-pip
  wget
  git
  curl
  htop
  iftop
  iotop
  locales
  locales-all
  vim
  gdb
  valgrind
  libgoogle-glog-dev
  cmake
  rsync
  iptables
  tcpdump
  cgroup-tools
  google-perftools
  libgoogle-perftools-dev
  linux-tools-common
  linux-tools-generic
  libbpf-dev
  clang
  sysstat
  cloud-guest-utils
  ssh
)

# --- Non-interactive behaviour ----------------------------------------------
#
# init.sh is meant to run unattended over SSH, so make sure nothing in the
# package tooling can block waiting for a human.
export DEBIAN_FRONTEND=noninteractive
export NEEDRESTART_MODE=a
export NEEDRESTART_SUSPEND=1
APT_OPTS=(-y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold)

# --- SSH behaviour -----------------------------------------------------------
#
# `accept-new` silently trusts (and records) a host key the first time a host is
# seen, which is what removes the interactive fingerprint prompt, while still
# failing loudly if a *known* key ever changes.
LITELIB_SSH_OPTS=${LITELIB_SSH_OPTS:-"-o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 -o ServerAliveInterval=30 -o ServerAliveCountMax=20"}

# --- Progress reporting ------------------------------------------------------
#
# init.sh and its sub-scripts announce what they are doing by echoing a marker
# at the *start of a line*.  setup_cluster.sh greps for it to show live
# progress while the nodes are being set up in parallel.  Anchoring on '^' is
# what keeps the `set -x` trace of the echo itself from being picked up, so the
# marker must stay free of regular-expression metacharacters (no brackets!).
LITELIB_PHASE_MARKER=${LITELIB_PHASE_MARKER:-">>> LITELIB:"}

# litelib_phase <text> -- announce the phase that is about to start.
litelib_phase() {
  echo "${LITELIB_PHASE_MARKER} $*"
}
