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
  cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBPFTOOL=${HOME}/dependencies/bpf-tools/bpftool
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
  sudo chown -R $(whoami):$(id -gn) ${HOME}/dependencies
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
  echo "LevelDB git version is wrong, please checkout manually"
  cd ../src/tests/redis-leveldb
  cd ./vendor/leveldb
  git checkout 99b3c03b3284f5886f9ef9a4ef703d57373e61be
  git submodule update --init --progress --recursive
  cd ../../
  git apply ../scripts/leveldb/redis-leveldb.patch
  make -j${NUM_JOBS}
  make test
}

build_libbpf() {
  CURRENT_DIR=$(pwd)
  mkdir -p ${HOME}/dependencies/libbpf
  cd ${HOME}/dependencies/libbpf
  wget https://github.com/libbpf/libbpf/archive/refs/tags/v1.5.0.tar.gz
  tar -xazf v1.5.0.tar.gz
  cd libbpf-1.5.0/src
  make -j${NUM_JOBS}
  sudo make install
  cd $CURRENT_DIR
}

get_bpf_tools() {
  CURRENT_DIR=$(pwd)
  mkdir -p ${HOME}/dependencies/bpf-tools
  cd ${HOME}/dependencies/bpf-tools
  wget https://github.com/libbpf/bpftool/releases/download/v7.5.0/bpftool-v7.5.0-amd64.tar.gz
  tar -xazf bpftool-v7.5.0-amd64.tar.gz
  chmod +x bpftool
  echo "Please add ${HOME}/dependencies/bpf-tools/ to your PATH"
  export PATH=${HOME}/dependencies/bpf-tools/:$PATH
  cd $CURRENT_DIR
}

main() {
  check_not_root
  install_dependencies
  install_criu
  build_redis_leveldb
  build_libbpf
  get_bpf_tools
  build_lite_version

  echo "Please do ssh-copy-id to the client node"
  echo "LevelDB server initialization is done."
}

main
