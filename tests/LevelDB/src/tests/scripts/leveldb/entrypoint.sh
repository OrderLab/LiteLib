#!/bin/sh

apt-get update

apt-get install -y \
    libsnappy-dev \
    libev-dev \
    libgmp-dev \
    cpanminus \
    perl \
    procps \
    openssh-client \
    openssh-server \
    python3 \
    python3-pip \
    tar \
    cmake \
    htop \
    iotop \
    iftop \
    tcpdump \
    criu \
    iptables \
    rsync

cpanm --quiet --notest --skip-satisfied --force Redis

cp /workspace/scripts/.ssh ~ -r
chmod 600 ~/.ssh/id_ed25519
service ssh start

cd /workspace/redis-leveldb
make -j
make test

pip3 install psutil redis --break-system-packages

# /opt/redis-leveldb/redis-leveldb -P 6379
tail -f /dev/null