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

  # Remove /mydata and resize the root filesystem
  exec ./resize_rootfs.sh
}

main
