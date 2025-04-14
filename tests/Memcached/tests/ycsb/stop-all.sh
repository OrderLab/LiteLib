#!/bin/bash

pkill -f memcached
pkill -f memcached-vanilla
pkill -f LiteMemcached

rm -f /tmp/memcached.sock
rm -f /tmp/lite_memcached
rm -f /dev/shm/lite_shared_memory

echo "All Memcached and LiteMemcached processes have been terminated."
