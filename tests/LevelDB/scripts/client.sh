#!/bin/bash
set -euo pipefail
set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUST_VERSION=${AE_RUST_VERSION:-1.88.0}

check_not_root() {
  if [ "$(id -u)" -eq 0 ]; then
    echo "This script should not be run as root. Please run as a regular user."
    exit 1
  fi
}

install_rust() {
  sudo apt-get update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    --no-install-recommends build-essential curl

  if [ ! -x "${HOME}/.cargo/bin/rustup" ]; then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs |
      sh -s -- -y --profile minimal --default-toolchain "${RUST_VERSION}"
  else
    "${HOME}/.cargo/bin/rustup" toolchain install \
      --profile minimal "${RUST_VERSION}"
  fi
  "${HOME}/.cargo/bin/rustup" default "${RUST_VERSION}"
}

compile_client() {
  cd "${SCRIPT_DIR}/../src/tests/client"
  "${HOME}/.cargo/bin/cargo" build --release
}

main() {
  check_not_root
  install_rust
  compile_client

  echo "LevelDB client initialization is done."
}

main
