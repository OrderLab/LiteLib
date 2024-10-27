#!/bin/bash

set -e
set -x

check_not_root() {
  if [ "$(id -u)" -eq 0 ]; then
    echo "This script should not be run as root. Please run as a regular user."
    exit 1
  fi
}

install_rust() {
  sudo apt-get install -y --no-install-recommends rust-all
}

compile_client() {
  cd ../src/tests/client
  cargo build --release
}

main() {
  check_not_root
  install_rust
  compile_client
}

main
