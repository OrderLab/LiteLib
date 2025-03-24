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
  # Remove existing rust installation
    sudo apt remove -y rust-all
    
    # Install curl if not already installed
    sudo apt install -y curl
    
    # Install rustup (the recommended way to install Rust)
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    
    # Add cargo to path for current session
    source "$HOME/.cargo/env"
    
    # Update to latest stable Rust
    rustup default stable
}

compile_client() {
  cd ../src/tests/client
  cargo build --release
}

main() {
  check_not_root
  install_rust
  compile_client

  echo "Please do ssh-copy-id to the server node"
  echo "LevelDB client initialization is done."
}

main
