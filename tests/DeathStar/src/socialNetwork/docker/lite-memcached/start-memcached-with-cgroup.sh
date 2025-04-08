#!/bin/bash

set -x

id=$1

cgcreate -g cpu:/deathstar_cpulimited_$id
cgset -r cpu.max="50000 100000" deathstar_cpulimited_$id
cgget -g cpu:/deathstar_cpulimited_$id

pip3 install psutil

exec tail -f /dev/null