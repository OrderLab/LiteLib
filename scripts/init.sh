#!/bin/bash
#
# Prepare a single node for the LiteLib experiments.  Safe to re-run: every
# step is idempotent, and the expensive source builds are skipped when the
# requested version is already installed.
#
# Run it as root, e.g. `sudo ./init.sh`, or through ../scripts/setup_cluster.sh
# to initialize the whole cluster at once.

set -e
set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

check_root() {
  if [ "$(id -u)" != "0" ]; then
    echo "This script must be run as root" 1>&2
    exit 1
  fi
}

prepare() {
  litelib_phase "refreshing apt package index"
  apt-get update
}

main() {
  check_root

  prepare

  # Setup network rate limit on shared control network
  litelib_phase "step 1/3: limiting the shared control network"
  "${SCRIPT_DIR}/network_limit.sh"

  # Remove /mydata and resize the root filesystem
  litelib_phase "step 2/3: resizing the root filesystem"
  "${SCRIPT_DIR}/resize_rootfs.sh"

  # Install dependencies for litesys
  litelib_phase "step 3/3: installing dependencies"
  "${SCRIPT_DIR}/litesys_dependency.sh"

  litelib_phase "initialization complete"
  echo "Initialization complete."
}

main
