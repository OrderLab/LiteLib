#!/bin/sh

# apt update
# apt -y install git
# apt -y install make automake libtool pkg-config libaio-dev
# apt -y install default-libmysqlclient-dev libssl-dev

# git clone https://github.com/akopytov/sysbench.git
# cd sysbench
# git checkout 805825fa81f633a7477f15ecdc152441e4ef4c83
# git apply /workspace/tests/MySQL/src/tests/scripts/client/sysbench.patch

# Current sysbench behavior:
# Average latency: cumulative (since test start) — the last intermediate report shows the overall average.
# P95 latency: interval-based (since last report) — each report shows the P95 for that interval.

# ./autogen.sh
# ./configure
# make -j
# make install

cp /workspace/tests/MySQL/src/tests/scripts/client/oltp_common.lua /usr/share/sysbench/

tail -f /dev/null