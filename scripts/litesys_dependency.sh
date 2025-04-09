#!/bin/bash

set -e
set -x

BOOST_VERSION=1.85.0
LIBEVENT_VERSION=2.1.12
NUM_JOBS=32

install_dependencies() {
  apt-get install -y --no-install-recommends \
    build-essential \
    software-properties-common \
    autoconf \
    automake \
    libtool \
    pkg-config \
    ca-certificates \
    libssl-dev \
    python3 \
    python3-dev \
    python3-pip \
    wget \
    git \
    curl \
    htop \
    iftop \
    iotop \
    locales \
    locales-all \
    vim \
    gdb \
    valgrind \
    libgoogle-glog-dev \
    cmake \
    rsync \
    iptables \
    tcpdump \
    cgroup-tools \
    google-perftools \
    libgoogle-perftools-dev \
    linux-tools-common \
    linux-tools-generic \
    linux-tools-$(uname -r) \
    libbpf-dev \
    clang \
    sysstat \
    linux-image-6.8.0-52-generic=6.8.0-52.53~22.04.1 \
    linux-image-6.8.0-52-generic=6.8.0-52.53~22.04.1
}

install_boost() {
  mkdir -p ${HOME}/dependencies/boost
  cd ${HOME}/dependencies/boost
  BOOST_VERSION_MOD=$(echo $BOOST_VERSION | tr . _)
  wget https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz
  tar -xavf boost-${BOOST_VERSION}-cmake.tar.xz
  cd boost-${BOOST_VERSION}
  ./bootstrap.sh --prefix=/usr/local
  ./b2 install
}

install_libevent() {
  mkdir -p ${HOME}/dependencies/libevent
  cd ${HOME}/dependencies/libevent
  wget "https://github.com/libevent/libevent/releases/download/release-${LIBEVENT_VERSION}-stable/libevent-${LIBEVENT_VERSION}-stable.tar.gz"
  tar -xzvf libevent-${LIBEVENT_VERSION}-stable.tar.gz
  cd libevent-${LIBEVENT_VERSION}-stable
  mkdir build && cd build
  cmake ..
  make -j${NUM_JOBS}
  make install
}

configure_ssh() {
  apt-get install -y --no-install-recommends ssh
  mkdir -p ${HOME}/.ssh
  chmod 700 ${HOME}/.ssh
  touch ${HOME}/.ssh/authorized_keys
  # echo "#PasswordAuthentication no" >>/etc/ssh/sshd_config
  # echo "PermitRootLogin yes" >>/etc/ssh/sshd_config
}

main() {
  install_dependencies
  install_boost
  install_libevent
  configure_ssh

  echo "Dependencies installed successfully!"
}

main
