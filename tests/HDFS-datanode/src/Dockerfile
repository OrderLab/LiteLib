FROM debian:12-slim

ARG BOOST_VERSION=1.83.0
ARG CMAKE_VERSION=3.27.7
ARG NUM_JOBS=32
ARG PROTOBUF_VERSION=21.12

ENV DEBIAN_FRONTEND noninteractive

# Install package dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        software-properties-common \
        autoconf \
        automake \
        libtool \
        pkg-config \
        ca-certificates \
        libssl-dev \
        wget \
        git \
        curl \
        locales \
        locales-all \
        vim \
        gdb \
        libgoogle-glog-dev \
        valgrind && \
    apt-get clean

# System locale
# Important for UTF-8
ENV LC_ALL en_US.UTF-8
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US.UTF-8

# Install CMake
RUN cd /tmp && \
    wget https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}.tar.gz && \
    tar xzf cmake-${CMAKE_VERSION}.tar.gz && \
    cd cmake-${CMAKE_VERSION} && \
    ./bootstrap && \
    make -j${NUM_JOBS} && \
    make install && \
    rm -rf /tmp/*

# Install Boost
# https://www.boost.org/doc/libs/1_80_0/more/getting_started/unix-variants.html
RUN cd /tmp && \
    BOOST_VERSION_MOD=$(echo $BOOST_VERSION | tr . _) && \
    wget https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}.tar.xz && \
    tar -xavf boost-${BOOST_VERSION}.tar.xz && \
    cd boost-${BOOST_VERSION} && \
    ./bootstrap.sh --prefix=/usr/local && \
    ./b2 install && \
    rm -rf /tmp/*

# Install libevent
RUN wget https://mirror.ghproxy.com/https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz && \
    tar -xavf libevent-2.1.12-stable.tar.gz && \
    cd libevent-2.1.12-stable && \
    mkdir build && cd build && \
    cmake .. && \
    make -j${NUM_JOBS} && \
    make install && \
    cd ../.. && \
    rm -rf libevent-2.1.12-stable

ENV CMAKE_PREFIX_PATH="/usr/local:$CMAKE_PREFIX_PATH"

# Install Protobuf
RUN cd /tmp && \
    wget https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOBUF_VERSION}/protobuf-cpp-${PROTOBUF_VERSION}.tar.gz && \
    tar -xavf protobuf-cpp-${PROTOBUF_VERSION}.tar.gz && \
    cd protobuf-${PROTOBUF_VERSION} && \
    ./configure && \
    make -j${NUM_JOBS} && \
    make install && \
    ldconfig && \
    cd .. && \
    rm -rf protobuf-${PROTOBUF_VERSION}*

# Install SSH
RUN apt-get install -y --no-install-recommends \
        ssh && \
    apt-get clean && \
    mkdir -p /root/.ssh && \
    chmod 700 /root/.ssh && \
    touch /root/.ssh/authorized_keys && \
    echo "#PasswordAuthentication no" >> /etc/ssh/sshd_config && \
    echo "PermitRootLogin yes" >> /etc/ssh/sshd_config