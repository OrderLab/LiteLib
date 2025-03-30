#!/bin/bash

set -x

id=$1

cgcreate -g cpu:/deathstar_cpulimited_$id
cgset -r cpu.max="100000 100000" deathstar_cpulimited_$id
cgget -g cpu:/deathstar_cpulimited_$id

/workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh $id

exec tail -f /dev/null