#!/bin/bash

set -e
set -x

NUM_JOBS=32

check_not_root() {
  if [ "$(id -u)" -eq 0 ]; then
    echo "This script should not be run as root. Please run as a regular user."
    exit 1
  fi
}

build_lite_version() {
  cd ../src/lite-version
  mkdir -p build
  cd build
  cmake .. -DCMAKE_BUILD_TYPE=Release
  make -j${NUM_JOBS}
}

install_dependencies() {
  sudo apt-get install -y --no-install-recommends \
    criu \
    libsnappy-dev \
    libev-dev \
    libgmp-dev \
    cpanminus \
    perl \
    procps
  sudo cpanm --quiet --notest --skip-satisfied --force Redis
  pip3 install psutil redis matplotlib
}

build_redis_leveldb() {
  cd ../src/tests/redis-leveldb
  git apply ../scripts/leveldb/redis-leveldb.patch
  make -j${NUM_JOBS}
  make test
}

main() {
  check_not_root
  # build_lite_version
  # install_dependencies
  build_redis_leveldb
}

main
