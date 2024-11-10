#!/bin/bash

set -e
set -x

NUM_JOBS=${1:-"40"}
WARM_UP_SIZE=${2:-"140000"}

if [ "$(id -u)" -eq 0 ]; then
  echo "This script should not be run as root. Please run as a regular user."
  exit 1
fi

sudo apt-get -y install memcached libmemcached-tools
sudo systemctl stop memcached
sudo systemctl disable memcached
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
pip3 install pymemcache python-binary-memcached psutil

cp ../Memcached_codes/warm_up_cache.py.template ../Memcached_codes/warm_up_cache.py
sed -i "/warm_up_size =/c\warm_up_size = $WARM_UP_SIZE" ../Memcached_codes/warm_up_cache.py
ln -s "`pwd`/../Memcached_codes/warm_up_cache.py" ~/warm_up_cache.py
ln -s "`pwd`/../Memcached_codes/crash.py" ~/crash.py
ln -s "`pwd`/../Memcached_codes/monitor.py" ~/monitor.py
ln -s "`pwd`/../Memcached_codes/init.py" ~/init.py

cd ../../lite-version
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j${NUM_JOBS}
ln -s "`pwd`/LiteMemcached" ~/LiteMemcached
ln -s "`pwd`/Lite/lite_cli" ~/lite_cli

echo "Done executing setup_memcached.sh"