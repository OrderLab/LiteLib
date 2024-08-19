#!/bin/sh

apt update
apt -y install git
apt -y install make automake libtool pkg-config libaio-dev
apt -y install default-libmysqlclient-dev libssl-dev

git clone https://github.com/akopytov/sysbench.git
cd sysbench
git checkout de18a036cc65196b1a4966d305f33db3d8fa6f8e
git apply /workspace/tests/MySQL/src/tests/scripts/client/sysbench.patch

./autogen.sh
./configure
make -j
make install

tail -f /dev/null