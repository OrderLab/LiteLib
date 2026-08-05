#!/bin/bash
set -euo pipefail
set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LEVELDB_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
NUM_JOBS=${AE_JOBS:-32}
REDIS_LEVELDB_COMMIT=403cc6eee547a13a4b49b79b30d52bedccc04c84
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
    -DCMAKE_BUILD_TYPE=Release
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
    libsnappy-dev \
    libev-dev \
    libgmp-dev \
    cpanminus \
    perl \
    procps \
    python3-pip \
    redis-tools
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
  # The repository gitlink is the wrong source version on some checkouts.
  # Always clone and manually check out the version used by the experiment.
  rm -rf "${REDIS_LEVELDB_DIR}"
  git clone -q https://github.com/KDr2/redis-leveldb.git \
    "${REDIS_LEVELDB_DIR}"
  git -C "${REDIS_LEVELDB_DIR}" checkout -q --detach \
    "${REDIS_LEVELDB_COMMIT}"
  git -C "${REDIS_LEVELDB_DIR}" submodule update --init --recursive

  cd "${REDIS_LEVELDB_DIR}"
  make -j"${NUM_JOBS}"
  cp -f redis-leveldb redis-leveldb-vanilla
  git apply "${REDIS_LEVELDB_PATCH}"
  make clean
  make -j"${NUM_JOBS}"
  make test
  test -x redis-leveldb -a -x redis-leveldb-vanilla
}

main() {
  check_not_root
  install_dependencies
  build_lite_version
  install_criu
  build_redis_leveldb

  echo "LevelDB server initialization is done."
}

main
