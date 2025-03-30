#!/bin/bash

set -x

Dir=$(dirname $0)
id=$1

$Dir/stop-all.sh

cgexec -g cpu:deathstar_cpulimited_$id memcached -m 16384 -t 8 -I 32m -c 4096 -u root &
