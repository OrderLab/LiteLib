#!/bin/bash

/workspace/tests/Memcached/src/lite-version-ascii/build/Lite/lite_cli -t /tmp/lite_memcached -p /tmp/memcached.sock -m 1

pgrep "memcached" | xargs kill -15
rm -rf /tmp/memcached.sock
