#!/bin/bash

/workspace/tests/Memcached/src/lite-version/build/Lite/lite_cli -t /tmp/lite_memcached -p /tmp/memcached.sock -m 1

pgrep "memcached" | xargs kill -9
rm -rf /tmp/memcached.sock
