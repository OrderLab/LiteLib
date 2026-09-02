#!/bin/bash
#
# Verify that scripts/init.sh has completed successfully on *this* node.
#
# The system checks are split into two groups:
#
#   [persistent]  survives a reboot -- distro packages, the pinned kernel,
#                 Boost/libevent under /usr/local, the grown root filesystem.
#   [runtime]     lives only in kernel state and must be re-applied after every
#                 reboot -- CPU frequency pinning, tc/iptables rate limits.
#
# User checks cover the evaluator account's SSH material and repository
# checkout.  They never install or modify system state.
#
# Exit status:
#   0  everything required is in place
#   1  at least one requested check failed
#   2  usage error
#
# Usage: ./check_init.sh [--persistent-only|--runtime-only]
#                        [--system-only|--user-only] [--quiet]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

STATE_MODE=all
SCOPE=all
QUIET=0

while [ $# -gt 0 ]; do
  case "$1" in
  --persistent-only)
    [ "${STATE_MODE}" = all ] || {
      echo "--persistent-only and --runtime-only are mutually exclusive" 1>&2
      exit 2
    }
    STATE_MODE=persistent
    ;;
  --runtime-only)
    [ "${STATE_MODE}" = all ] || {
      echo "--persistent-only and --runtime-only are mutually exclusive" 1>&2
      exit 2
    }
    STATE_MODE=runtime
    ;;
  --system-only)
    [ "${SCOPE}" = all ] || {
      echo "--system-only and --user-only are mutually exclusive" 1>&2
      exit 2
    }
    SCOPE=system
    ;;
  --user-only)
    [ "${SCOPE}" = all ] || {
      echo "--system-only and --user-only are mutually exclusive" 1>&2
      exit 2
    }
    SCOPE=user
    ;;
  -q | --quiet) QUIET=1 ;;
  -h | --help)
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  *)
    echo "unknown argument: $1" 1>&2
    exit 2
    ;;
  esac
  shift
done

if [ "${SCOPE}" = user ] && [ "${STATE_MODE}" = runtime ]; then
  echo "--user-only cannot be combined with --runtime-only" 1>&2
  exit 2
fi

FAILED=0
FAILED_CHECKS=()

if [ -t 1 ] && [ "${TERM:-dumb}" != "dumb" ]; then
  C_OK=$'\033[32m'
  C_BAD=$'\033[31m'
  C_OFF=$'\033[0m'
else
  C_OK=""
  C_BAD=""
  C_OFF=""
fi

log() { [ "${QUIET}" -eq 1 ] || echo "$@"; }

# report <name> <status:0|1> [detail]
report() {
  local name=$1 status=$2 detail=${3:-}
  if [ "${status}" -eq 0 ]; then
    log "  ${C_OK}[ OK ]${C_OFF} ${name}${detail:+  (${detail})}"
  else
    log "  ${C_BAD}[FAIL]${C_OFF} ${name}${detail:+  -- ${detail}}"
    FAILED=$((FAILED + 1))
    FAILED_CHECKS+=("${name}")
  fi
}

# ---------------------------------------------------------------------------
# Persistent checks
# ---------------------------------------------------------------------------

check_packages() {
  local missing=()
  local pkg
  for pkg in "${LITELIB_APT_PACKAGES[@]}"; do
    if ! dpkg-query -W -f='${Status}' "${pkg}" 2>/dev/null | grep -q "ok installed"; then
      missing+=("${pkg}")
    fi
  done
  if [ ${#missing[@]} -eq 0 ]; then
    report "distro packages (${#LITELIB_APT_PACKAGES[@]})" 0
  else
    report "distro packages" 1 "missing: ${missing[*]}"
  fi
}

check_kernel_installed() {
  local pkg="linux-image-${LITELIB_KERNEL_RELEASE}"
  if dpkg-query -W -f='${Status}' "${pkg}" 2>/dev/null | grep -q "ok installed"; then
    report "kernel ${LITELIB_KERNEL_RELEASE} installed" 0
  else
    report "kernel ${LITELIB_KERNEL_RELEASE} installed" 1 "run init.sh"
  fi
  if dpkg-query -W -f='${Status}' "linux-headers-${LITELIB_KERNEL_RELEASE}" 2>/dev/null | grep -q "ok installed"; then
    report "kernel headers ${LITELIB_KERNEL_RELEASE}" 0
  else
    report "kernel headers ${LITELIB_KERNEL_RELEASE}" 1 "run init.sh"
  fi
  if dpkg-query -W -f='${Status}' "linux-tools-${LITELIB_KERNEL_RELEASE}" 2>/dev/null | grep -q "ok installed"; then
    report "kernel tools ${LITELIB_KERNEL_RELEASE}" 0
  else
    report "kernel tools ${LITELIB_KERNEL_RELEASE}" 1 "run init.sh"
  fi
}

check_boost() {
  local header="${LITELIB_PREFIX}/include/boost/version.hpp"
  if [ ! -f "${header}" ]; then
    report "boost ${LITELIB_BOOST_VERSION}" 1 "${header} not found"
    return
  fi
  local found
  found=$(sed -n 's/.*BOOST_LIB_VERSION "\(.*\)".*/\1/p' "${header}" | head -1)
  local expected
  expected=$(echo "${LITELIB_BOOST_VERSION}" | tr . _)
  expected=${expected%_0}
  if [ "${found}" = "${expected}" ] &&
    ls "${LITELIB_PREFIX}"/lib/libboost_system.so* >/dev/null 2>&1; then
    report "boost ${LITELIB_BOOST_VERSION}" 0 "${LITELIB_PREFIX}"
  else
    report "boost ${LITELIB_BOOST_VERSION}" 1 "found '${found}', libs $(ls "${LITELIB_PREFIX}"/lib/libboost_system.so* 2>/dev/null | wc -l)"
  fi
}

check_libevent() {
  local header="${LITELIB_PREFIX}/include/event2/event-config.h"
  if [ ! -f "${header}" ]; then
    report "libevent ${LITELIB_LIBEVENT_VERSION}" 1 "${header} not found"
    return
  fi
  if grep -q "EVENT__VERSION \"${LITELIB_LIBEVENT_VERSION}" "${header}" &&
    ls "${LITELIB_PREFIX}"/lib/libevent.so >/dev/null 2>&1; then
    report "libevent ${LITELIB_LIBEVENT_VERSION}" 0 "${LITELIB_PREFIX}"
  else
    report "libevent ${LITELIB_LIBEVENT_VERSION}" 1 "version mismatch or library missing"
  fi
}

check_rootfs() {
  local gib
  gib=$(df -BG --output=size / | tail -1 | tr -dc '0-9')
  if [ -n "${gib}" ] && [ "${gib}" -ge "${LITELIB_MIN_ROOT_GIB}" ]; then
    report "root filesystem grown" 0 "${gib} GiB"
  else
    report "root filesystem grown" 1 "${gib:-?} GiB < ${LITELIB_MIN_ROOT_GIB} GiB, run resize_rootfs.sh"
  fi
  if grep -q '/mydata' /etc/fstab; then
    report "/mydata removed from /etc/fstab" 1 "stale entry present"
  else
    report "/mydata removed from /etc/fstab" 0
  fi
}

check_docker() {
  if ! command -v docker >/dev/null 2>&1; then
    report "Docker daemon" 1 "docker command not found, run init.sh"
    return
  fi
  if systemctl is-active --quiet docker &&
    docker info >/dev/null 2>&1; then
    report "Docker daemon" 0 "$(docker version --format '{{.Server.Version}}' 2>/dev/null)"
  else
    report "Docker daemon" 1 "service unavailable, run init.sh"
  fi
}

check_ssh() {
  local ssh_dir="${LITELIB_USER_HOME}/.ssh"
  if [ -s "${ssh_dir}/authorized_keys" ]; then
    report "SSH authorized_keys present" 0
  else
    report "SSH authorized_keys present" 1 "${ssh_dir}/authorized_keys missing or empty"
  fi
  if [ -s "${LITELIB_SSH_KEY}" ] && [ -s "${LITELIB_SSH_KEY}.pub" ]; then
    report "cluster SSH keypair present" 0
  else
    report "cluster SSH keypair present" 1 "${LITELIB_SSH_KEY}{,.pub} must both exist"
  fi

  local missing=() node configured_nodes=()
  read -r -a configured_nodes <<<"${LITELIB_NODES}"
  for node in "${configured_nodes[@]}"; do
    if ! ssh-keygen -F "${node}" -f "${ssh_dir}/known_hosts" >/dev/null 2>&1; then
      missing+=("${node}")
    fi
  done
  if [ "${#missing[@]}" -eq 0 ]; then
    report "node alias host keys" 0 "${LITELIB_NODES}"
  else
    report "node alias host keys" 1 "missing: ${missing[*]}"
  fi
}

check_repo() {
  if [ -d "${LITELIB_REPO_DIR}/.git" ]; then
    report "LiteLib repository checkout" 0 "${LITELIB_REPO_DIR}"
  else
    report "LiteLib repository checkout" 1 "${LITELIB_REPO_DIR}/.git not found"
  fi
}

# ---------------------------------------------------------------------------
# Runtime checks (reset by a reboot)
# ---------------------------------------------------------------------------

check_kernel_booted() {
  local running
  running=$(uname -r)
  if [ "${running}" = "${LITELIB_KERNEL_RELEASE}" ]; then
    report "booted into ${LITELIB_KERNEL_RELEASE}" 0
  else
    report "booted into ${LITELIB_KERNEL_RELEASE}" 1 "running ${running}, reboot required"
  fi
}

check_cpu_frequency() {
  local gov
  gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
  if [ "${gov}" = "performance" ]; then
    report "cpu governor" 0 "performance"
  else
    report "cpu governor" 1 "'${gov:-unknown}', re-run init.sh"
  fi

  local want_khz min_khz max_khz
  want_khz=$(awk -v f="${LITELIB_CPU_FREQ_GHZ}" 'BEGIN { printf "%d", f * 1000000 }')
  min_khz=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq 2>/dev/null)
  max_khz=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null)
  # intel_pstate rounds to the nearest 100MHz step, so allow a small delta.
  if [ -n "${min_khz}" ] && [ -n "${max_khz}" ] &&
    [ "$((min_khz > want_khz ? min_khz - want_khz : want_khz - min_khz))" -le 100000 ] &&
    [ "$((max_khz > want_khz ? max_khz - want_khz : want_khz - max_khz))" -le 100000 ]; then
    report "cpu frequency pinned to ${LITELIB_CPU_FREQ_GHZ}GHz" 0 "${min_khz}-${max_khz} kHz"
  else
    report "cpu frequency pinned to ${LITELIB_CPU_FREQ_GHZ}GHz" 1 "${min_khz:-?}-${max_khz:-?} kHz, re-run init.sh"
  fi
}

check_tc() {
  local qdisc
  qdisc=$(tc qdisc show dev "${LITELIB_CTRL_IFACE}" 2>/dev/null | grep -m1 'qdisc htb 1:')
  if [ -z "${qdisc}" ]; then
    report "tc htb shaping on ${LITELIB_CTRL_IFACE}" 1 "no htb root qdisc, re-run network_limit.sh"
    return
  fi
  local rate
  rate=$(tc class show dev "${LITELIB_CTRL_IFACE}" 2>/dev/null | sed -n 's/.*rate \([^ ]*\).*/\1/p' | head -1)
  if [ -n "${rate}" ]; then
    report "tc htb shaping on ${LITELIB_CTRL_IFACE}" 0 "rate ${rate}"
  else
    report "tc htb shaping on ${LITELIB_CTRL_IFACE}" 1 "htb qdisc without a class, re-run network_limit.sh"
  fi
}

check_iptables() {
  if [ "$(id -u)" != "0" ]; then
    report "iptables rate limits" 1 "need root to inspect; re-run as 'sudo ./check_init.sh'"
    return
  fi
  local out_ok=0 in_ok=0
  iptables -S OUTPUT 2>/dev/null | grep -q 'limit_pub_pkts' && out_ok=1
  iptables -S INPUT 2>/dev/null | grep -q 'limit_input_pkts' && in_ok=1
  if [ "${out_ok}" -eq 1 ] && [ "${in_ok}" -eq 1 ]; then
    report "iptables rate limits" 0 "${LITELIB_PKTS_PER_SEC} pkts/sec"
  else
    report "iptables rate limits" 1 "OUTPUT=${out_ok} INPUT=${in_ok}, re-run network_limit.sh"
  fi
}

# ---------------------------------------------------------------------------

log "==> LiteLib initialization check on $(hostname -s) ($(date -Is))"

if { [ "${SCOPE}" = all ] || [ "${SCOPE}" = system ]; } &&
  { [ "${STATE_MODE}" = all ] || [ "${STATE_MODE}" = persistent ]; }; then
  log "-- system persistent state --"
  check_packages
  check_kernel_installed
  check_boost
  check_libevent
  check_rootfs
  check_docker
fi

if { [ "${SCOPE}" = all ] || [ "${SCOPE}" = system ]; } &&
  { [ "${STATE_MODE}" = all ] || [ "${STATE_MODE}" = runtime ]; }; then
  log "-- system runtime state (re-apply with post_reboot.sh after every reboot) --"
  check_kernel_booted
  check_cpu_frequency
  check_tc
  check_iptables
fi

if { [ "${SCOPE}" = all ] || [ "${SCOPE}" = user ]; } &&
  [ "${STATE_MODE}" != runtime ]; then
  log "-- user state --"
  check_ssh
  check_repo
fi

if [ "${FAILED}" -eq 0 ]; then
  log "==> $(hostname -s): initialized"
  exit 0
fi

log "==> $(hostname -s): NOT initialized (${FAILED} failing check(s): ${FAILED_CHECKS[*]})"
exit 1
