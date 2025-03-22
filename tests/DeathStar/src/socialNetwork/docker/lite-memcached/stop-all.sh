#!/bin/bash

pgrep "memcached" | xargs kill -9
pgrep "LiteMemcached" | xargs kill -9
pgrep "lite_cli" | xargs kill -9
rm -rf /tmp/memcached.sock
