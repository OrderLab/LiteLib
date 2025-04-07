#!/bin/bash

set -e
set -x

check_root() {
  if [ "$(id -u)" != "0" ]; then
    echo "This script must be run as root" 1>&2
    exit 1
  fi
}

prepare() {
  apt update
}

main() {
  check_root

  prepare

  # Setup network rate limit on shared control network
  ./network_limit.sh

  # Remove /mydata and resize the root filesystem
  ./resize_rootfs.sh

  # Install dependencies for litesys
  ./litesys_dependency.sh

  echo "Initialization complete."
}

main
