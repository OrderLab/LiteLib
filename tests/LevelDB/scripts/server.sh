#!/bin/bash
set -euo pipefail
set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LEVELDB_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
NUM_JOBS=${AE_JOBS:-32}
REDIS_LEVELDB_COMMIT=403cc6eee547a13a4b49b79b30d52bedccc04c84
LEVELDB_COMMIT=99b3c03b3284f5886f9ef9a4ef703d57373e61be
REDIS_LEVELDB_DIR="${LEVELDB_DIR}/src/tests/redis-leveldb"
REDIS_LEVELDB_PATCH="${LEVELDB_DIR}/src/tests/scripts/leveldb/redis-leveldb.patch"

check_not_root() {
  if [ "$(id -u)" -eq 0 ]; then
    echo "This script should not be run as root. Please run as a regular user."
    exit 1
  fi
}

build_lite_version() {
  cmake -S "${LEVELDB_DIR}/src/lite-version" \
    -B "${LEVELDB_DIR}/src/lite-version/build" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBPFTOOL="${HOME}/dependencies/bpf-tools/bpftool"
  cmake --build "${LEVELDB_DIR}/src/lite-version/build" -j"${NUM_JOBS}"
}

install_dependencies() {
  sudo apt-get update -qq
  sudo apt-get install -y --no-install-recommends \
    build-essential \
    cgroup-tools \
    cmake \
    git \
    wget \
    libelf-dev \
    libsnappy-dev \
    libev-dev \
    libgmp-dev \
    cpanminus \
    perl \
    procps \
    python3-pip \
    redis-tools \
    zlib1g-dev
  sudo cpanm --quiet --notest --skip-satisfied --force Redis
  python3 -m pip install --user psutil redis matplotlib
}

install_criu() {
  sudo apt-get install -y --no-install-recommends \
    libprotobuf-dev \
    libprotobuf-c-dev \
    protobuf-c-compiler \
    protobuf-compiler \
    python3-protobuf \
    pkg-config \
    libbsd-dev \
    iproute2 \
    libnftables-dev \
    libcap-dev \
    libnl-3-dev \
    libnet-dev \
    libaio-dev \
    libgnutls28-dev \
    python3-future \
    asciidoctor

  if command -v criu >/dev/null &&
      criu --version 2>/dev/null | grep -q 'Version: 4.0'; then
    return
  fi

  sudo mkdir -p "${HOME}/dependencies"
  sudo chown -R "$(id -u):$(id -g)" "${HOME}/dependencies"
  mkdir -p "${HOME}/dependencies/criu"
  cd "${HOME}/dependencies/criu"
  wget -q -O criu-4.0.tar.gz \
    https://github.com/checkpoint-restore/criu/archive/v4.0/criu-4.0.tar.gz
  rm -rf criu-4.0
  tar -xzf criu-4.0.tar.gz
  make -C criu-4.0 -j"${NUM_JOBS}"
  sudo make -C criu-4.0 install
}

build_redis_leveldb() {
  rm -rf "${REDIS_LEVELDB_DIR}"
  git clone -q https://github.com/KDr2/redis-leveldb.git \
    "${REDIS_LEVELDB_DIR}"
  git -C "${REDIS_LEVELDB_DIR}" checkout -q --detach \
    "${REDIS_LEVELDB_COMMIT}"
  git -C "${REDIS_LEVELDB_DIR}" submodule update --init --recursive
  git -C "${REDIS_LEVELDB_DIR}/vendor/leveldb" checkout -q --detach \
    "${LEVELDB_COMMIT}"
  git -C "${REDIS_LEVELDB_DIR}/vendor/leveldb" \
    submodule update --init --recursive

  cd "${REDIS_LEVELDB_DIR}"
  git apply "${REDIS_LEVELDB_PATCH}"
  make -j"${NUM_JOBS}"
  make test
  cp -f redis-leveldb redis-leveldb-vanilla
  test -x redis-leveldb-vanilla
}

build_libbpf() {
  if [ -e /usr/lib64/libbpf.so.1 ]; then
    return
  fi

  mkdir -p "${HOME}/dependencies/libbpf"
  cd "${HOME}/dependencies/libbpf"
  wget -q -O v1.5.0.tar.gz \
    https://github.com/libbpf/libbpf/archive/refs/tags/v1.5.0.tar.gz
  rm -rf libbpf-1.5.0
  tar -xzf v1.5.0.tar.gz
  make -C libbpf-1.5.0/src -j"${NUM_JOBS}"
  sudo make -C libbpf-1.5.0/src install
  sudo ldconfig
}

get_bpf_tools() {
  mkdir -p "${HOME}/dependencies/bpf-tools"
  if [ ! -x "${HOME}/dependencies/bpf-tools/bpftool" ]; then
    cd "${HOME}/dependencies/bpf-tools"
    wget -q -O bpftool-v7.5.0-amd64.tar.gz \
      https://github.com/libbpf/bpftool/releases/download/v7.5.0/bpftool-v7.5.0-amd64.tar.gz
    tar -xzf bpftool-v7.5.0-amd64.tar.gz
    chmod +x bpftool
  fi
}

main() {
  check_not_root
  install_dependencies
  install_criu
  build_libbpf
  get_bpf_tools
  build_lite_version
  build_redis_leveldb

  echo "LevelDB server initialization is done."
}

main
