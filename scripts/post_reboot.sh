#!/bin/bash
#
# Re-apply the LiteLib *runtime* configuration after a reboot.
#
# A reboot clears everything that lives only in kernel state:
#
#   * the `tc` bandwidth shaping and the `iptables` packet-rate limits that
#     keep the experiments isolated from the shared CloudLab control network,
#   * the CPU frequency pinning that keeps latency measurements stable.
#
# Running the experiments without these in place produces noticeably noisier
# results, so this must be re-run on **every** node after **every** reboot.
#
# This is a fast subset of init.sh: it touches no package manager and builds
# nothing, so it takes seconds instead of tens of minutes.  Running the full
# `init.sh` instead is equally correct, just slower.
#
# Usage: sudo ./post_reboot.sh

set -e
set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

check_root() {
  if [ "$(id -u)" != "0" ]; then
    echo "This script must be run as root, e.g. 'sudo $0'" 1>&2
    exit 1
  fi
}

set_cpu_frequency() {
  litelib_phase "re-pinning the CPU frequency to ${LITELIB_CPU_FREQ_GHZ}GHz"
  cpupower frequency-set -g performance
  cpupower frequency-set -d "${LITELIB_CPU_FREQ_GHZ}GHz" -u "${LITELIB_CPU_FREQ_GHZ}GHz"
}

main() {
  check_root

  litelib_phase "step 1/2: re-applying the control-network rate limits"
  "${SCRIPT_DIR}/network_limit.sh"

  litelib_phase "step 2/2: re-pinning the CPU frequency"
  set_cpu_frequency

  litelib_phase "runtime configuration re-applied"
  echo "Runtime configuration re-applied. Verify with: sudo ${SCRIPT_DIR}/check_init.sh"
}

main
