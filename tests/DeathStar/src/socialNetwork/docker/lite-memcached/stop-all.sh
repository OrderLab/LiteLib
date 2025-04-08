#!/bin/bash

set -x

pip3 install psutil

pgrep "memcached" | xargs kill -9
pgrep "LiteMemcached" | xargs kill -9
pgrep "lite_cli" | xargs kill -9
rm -f /tmp/memcached.sock
rm -f /tmp/lite_memcached
rm -f /dev/shm/lite_shared_memory
sleep 5 # wait for the sockets to be removed