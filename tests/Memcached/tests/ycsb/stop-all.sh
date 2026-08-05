#!/bin/bash

for name in memcached memcached-vanil LiteMemcached; do
  for pid in $(pgrep -x "$name" 2>/dev/null || true); do
    kill "$pid" 2>/dev/null || true
  done
done

rm -f /tmp/memcached.sock
rm -f /tmp/lite_memcached
rm -f /dev/shm/lite_shared_memory

sleep 2
echo "All Memcached and LiteMemcached processes have been terminated."
