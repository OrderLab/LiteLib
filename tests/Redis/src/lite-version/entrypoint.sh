#!/bin/bash

CMAKE_VERSION="3.27.7"
BOOST_VERSION="1.83.0"
LIBEVENT_VERSION="2.1.12-stable"

apt update
apt install -y build-essential wget libssl-dev git

cd /tmp

if ! which cmake >/dev/null 2>&1; then
{
    # echo "-----------------Installing CMake-----------------"
    wget https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.sh
    mkdir /opt/cmake
    sh cmake-$CMAKE_VERSION-linux-x86_64.sh --prefix=/opt/cmake --skip-license
    ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake
    rm cmake-$CMAKE_VERSION-linux-x86_64.sh
}
fi

if ! which boost >/dev/null 2>&1; then
{
    # echo "-----------------Installing Boost-----------------"
    wget https://github.com/boostorg/boost/releases/download/boost-$BOOST_VERSION/boost-$BOOST_VERSION.tar.gz
    tar xf boost-$BOOST_VERSION.tar.gz
    cd boost-$BOOST_VERSION
    ./bootstrap.sh --prefix=/usr/local --with-libraries=all
    ./b2 install
    cd ..
    rm -rf boost-$BOOST_VERSION boost-$BOOST_VERSION.tar.gz
}
fi

if ! which libevent >/dev/null 2>&1; then
{
    # echo "-----------------Installing Libevent-----------------"
    wget https://github.com/libevent/libevent/releases/download/release-$LIBEVENT_VERSION/libevent-$LIBEVENT_VERSION.tar.gz
    tar xf libevent-$LIBEVENT_VERSION.tar.gz
    cd libevent-$LIBEVENT_VERSION
    mkdir build && cd build
    cmake ..
    make
    make install
    cd ../..
    rm -rf libevent-$LIBEVENT_VERSION libevent-$LIBEVENT_VERSION.tar.gz
}
fi

mkdir -p /workspace/build
cd /workspace/build
cmake .. && make

echo "-----------------Building complete-----------------"