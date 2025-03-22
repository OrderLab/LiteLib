#!/bin/bash

Dir=$(dirname $0)

$Dir/stop-all.sh

memcached -m 16384 -t 8 -I 32m -c 4096 -u root &
