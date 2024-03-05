## Reference Model for Memcached

* https://github.com/boostorg/asio/blob/develop/example/cpp03/http/server3
* https://github.com/facebook/hhvm/blob/master/hphp/util/concurrent-lru-cache.h

### Environment

```sh
export BOOST_VERSION=1.83.0
docker build -f Dockerfile --build-arg BOOST_VERSION=${BOOST_VERSION} --tag=boost:${BOOST_VERSION} .
docker run --privileged --name memcached-dev -v .:/workspace -p 8080:22 -it boost:${BOOST_VERSION} /bin/bash

#inside docker
echo "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAINcZFT3064K5rGwyguWUb7oTMoHiogZRiXxR5h6le4jR yichencs@dell-yichencs" >> /root/.ssh/authorized_keys
echo "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIERwwdWQ79JOlU27J4wuC4xKk+vADhdOdwNaM9klmPcW toga@TOGA-ThinkpadT480s" >> /root/.ssh/authorized_keys
git config --global --add safe.directory /workspace

docker start memcached-dev
docker exec -it memcached-dev /bin/bash

#inside docker
service ssh start

wget "https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz"
tar -xzvf libevent-2.1.12-stable.tar.gz
cd libevent-2.1.12-stable
mkdir build && cd build
cmake .. # Default to Unix Makefiles
make
# make verify
make install
```

## Client

```sh
# apt install python3-pylibmc
# apt install libmemcached-dev p
# pip3 install pylibmc
pip3 install python-binary-memcached --break-system-packages
```
