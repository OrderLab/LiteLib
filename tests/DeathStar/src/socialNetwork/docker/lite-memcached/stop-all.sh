#!/bin/bash

set -x

pgrep "memcached" | xargs -r kill -9
pgrep "LiteMemcached" | xargs -r kill -9
pgrep "lite_cli" | xargs -r kill -9
rm -f /tmp/memcached.sock
rm -f /tmp/lite_memcached
rm -f /dev/shm/lite_shared_memory
sleep 5 # wait for the sockets to be removed