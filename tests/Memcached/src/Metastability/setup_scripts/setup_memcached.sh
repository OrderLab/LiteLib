apt update
apt -y install ssh
service ssh start
cp /workspace/.ssh /root -r
chmod 600 /root/.ssh/id_ed25519
WARM_UP_SIZE=${2:-"140000"}
apt-get update
apt-get -y install memcached libevent-dev
apt-get -y install libmemcached-tools
apt-get -y install ipvsadm iproute2
apt-get -y install libgoogle-glog-dev
ln -s /usr/bin/memcached /usr/bin/memcached.replica
# chmod 777 /etc/memcached.conf
# sed -i 's/-l 127.0.0.1/-l 0.0.0.0/' /etc/memcached.conf 
# sed -i 's/-m/#-m/' /etc/memcached.conf 
# echo "-m $CACHE_MEM_SIZE" >> /etc/memcached.conf 
# service memcached restart

ip addr add 10.0.233.7/24 dev eth0:0
ip net a testx
ip l a vx type veth peer name eth0 netns testx
ip net e testx ip l s lo up
ip net e testx ip l s eth0 up
ip net e testx ip a a 192.168.254.10/24 dev eth0
ip net e testx ip route replace default via 192.168.254.1
ip l s vx up
ip a a 192.168.254.1/24 dev vx
sysctl -w net.ipv4.vs.expire_nodest_conn=1

# memcached -d -u root -l 0.0.0.0 --enable-shutdown -m $CACHE_MEM_SIZE
apt-get -y install python3-pip
pip3 install pymemcache --break-system-packages
pip3 install python-binary-memcached --break-system-packages
pip3 install psutil --break-system-packages

sed -i "/warm_up_size =/c\warm_up_size = $WARM_UP_SIZE" ../Memcached_codes/warm_up_cache.py
ln -s "`pwd`/../Memcached_codes/warm_up_cache.py" ~/warm_up_cache.py
ln -s "`pwd`/../Memcached_codes/crash.py" ~/crash.py
ln -s "`pwd`/../Memcached_codes/monitor.py" ~/monitor.py
ln -s "`pwd`/../Memcached_codes/init.py" ~/init.py

# python3 ~/init.py

echo "Done executing setup_memcached.sh"