#!/bin/bash

apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        software-properties-common \
        autoconf \
        automake \
        libtool \
        pkg-config \
        ca-certificates \
        libssl-dev \
        wget \
        tar \
        git \
        curl \
        locales \
        locales-all \
        vim \
        gdb \
        libgoogle-glog-dev \
        valgrind \
        protobuf-compiler \
        libboost-all-dev \
        libprotobuf-dev &&
    apt-get clean

# Install Boost
# https://www.boost.org/doc/libs/1_80_0/more/getting_started/unix-variants.html
cd /tmp &&
    BOOST_VERSION_MOD=$(echo $BOOST_VERSION | tr . _) &&
    #    wget https://boostorg.jfrog.io/artifactory/main/release/1.83.0/source/boost_${BOOST_VERSION_MOD}.tar.bz2 && \
    wget https://github.com/boostorg/boost/releases/download/boost-1.83.0/boost-1.83.0.tar.xz &&
    tar -xavf boost-1.83.0.tar.xz &&
    cd boost-1.83.0 &&
    ./bootstrap.sh --prefix=/usr/local &&
    ./b2 install &&
    rm -rf /tmp/*

cd /tmp &&
    wget "https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz" &&
    tar -xzvf libevent-2.1.12-stable.tar.gz &&
    cd libevent-2.1.12-stable &&
    mkdir build && cd build &&
    cmake .. &&
    make &&
    make install