#!/bin/bash

set -e
set -x

NUM_JOBS=${1:-"40"}
WARM_UP_SIZE=${2:-"140000"}

if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
else
  SUDO="sudo"
fi

${SUDO} apt-get update
${SUDO} apt-get -y install \
  build-essential cmake git wget python3 python3-pip \
  sudo cgroup-tools \
  libboost-all-dev libevent-dev libgoogle-glog-dev \
  memcached libmemcached-tools
${SUDO} systemctl stop memcached || true
${SUDO} systemctl disable memcached || true
# apt-get -y install ipvsadm iproute2
# ln -s /usr/bin/memcached /usr/bin/memcached.replica

# chmod 777 /etc/memcached.conf
# sed -i 's/-l 127.0.0.1/-l 0.0.0.0/' /etc/memcached.conf 
# sed -i 's/-m/#-m/' /etc/memcached.conf 
# echo "-m $CACHE_MEM_SIZE" >> /etc/memcached.conf 
# service memcached restart

# ip addr add 10.0.233.7/24 dev eth0:0
# ip net a testx
# ip l a vx type veth peer name eth0 netns testx
# ip net e testx ip l s lo up
# ip net e testx ip l s eth0 up
# ip net e testx ip a a 192.168.254.10/24 dev eth0
# ip net e testx ip route replace default via 192.168.254.1
# ip l s vx up
# ip a a 192.168.254.1/24 dev vx
# sysctl -w net.ipv4.vs.expire_nodest_conn=1

# memcached -d -u root -l 0.0.0.0 --enable-shutdown -m $CACHE_MEM_SIZE
PIP_FLAGS=()
if pip3 help install 2>/dev/null | grep -q -- --break-system-packages; then
  PIP_FLAGS+=(--break-system-packages)
fi
pip3 install "${PIP_FLAGS[@]}" pymemcache python-binary-memcached psutil


${SUDO} apt-get install -y --no-install-recommends \
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
mkdir -p ${HOME}/dependencies
${SUDO} chown -R $(whoami):$(id -gn) ${HOME}/dependencies
mkdir -p ${HOME}/dependencies/criu
cd ${HOME}/dependencies/criu
wget http://github.com/checkpoint-restore/criu/archive/v4.0/criu-4.0.tar.gz
tar -xazf criu-4.0.tar.gz
cd criu-4.0
make -j16
${SUDO} make PREFIX=/usr install

cd $CURRENT_DIR

cp ../Memcached_codes/warm_up_cache.py.template ../Memcached_codes/warm_up_cache.py
sed -i "/warm_up_size =/c\warm_up_size = $WARM_UP_SIZE" ../Memcached_codes/warm_up_cache.py
ln -sfn "`pwd`/../Memcached_codes/warm_up_cache.py" ~/warm_up_cache.py
ln -sfn "`pwd`/../Memcached_codes/crash.py" ~/crash.py
ln -sfn "`pwd`/../Memcached_codes/monitor.py" ~/monitor.py
ln -sfn "`pwd`/../Memcached_codes/init.py" ~/init.py

cd /lite-version
mkdir -p build
cd build
echo "/usr/local/lib" > /etc/ld.so.conf.d/litelib.conf
ldconfig
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j${NUM_JOBS}
ln -sfn "`pwd`/LiteMemcached" ~/LiteMemcached
ln -sfn "`pwd`/Lite/lite_cli" ~/lite_cli

echo "Done executing setup_memcached.sh"