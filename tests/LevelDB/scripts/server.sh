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
  CURRENT_DIR=$(pwd)
  cd ../src/lite-version
  mkdir -p build
  cd build
  cmake .. -DCMAKE_BUILD_TYPE=Release
  make -j${NUM_JOBS}
  cd $CURRENT_DIR
}

install_dependencies() {
  sudo apt-get install -y --no-install-recommends \
    libsnappy-dev \
    libev-dev \
    libgmp-dev \
    cpanminus \
    perl \
    procps
  sudo cpanm --quiet --notest --skip-satisfied --force Redis
  pip3 install psutil redis matplotlib
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

  CURRENT_DIR=$(pwd)
  sudo chown -R $(whoami):$(whoami) ${HOME}/dependencies
  mkdir -p ${HOME}/dependencies/criu
  cd ${HOME}/dependencies/criu
  wget http://github.com/checkpoint-restore/criu/archive/v4.0/criu-4.0.tar.gz
  tar -xazf criu-4.0.tar.gz
  cd criu-4.0
  make -j${NUM_JOBS}
  sudo make install

  cd $CURRENT_DIR
}

build_redis_leveldb() {
  cd ../src/tests/redis-leveldb
  git apply ../scripts/leveldb/redis-leveldb.patch
  make -j${NUM_JOBS}
  make test
}

main() {
  check_not_root
  build_lite_version
  install_dependencies
  install_criu
  build_redis_leveldb
}

main
