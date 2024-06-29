#!/bin/bash

sysbench \
/usr/share/sysbench/oltp_read_write.lua \
--db-driver=mysql \
--tables=2 \
--table-size=1000000 \
--threads=1 \
--mysql-host=127.0.0.1 \
--mysql-port=60000 \
--mysql-user=sbtest \
--mysql-password=password \
prepare

sysbench \
/usr/share/sysbench/oltp_read_write.lua \
--db-driver=mysql \
--report-interval=2 \
--tables=2 \
--table-size=1000000 \
--threads=64 \
--time=10 \
--mysql-host=127.0.0.1 \
--mysql-port=60000 \
--mysql-user=sbtest \
--mysql-password=password \
run

# sysbench \
# --db-driver=mysql \
# --oltp-table-size=100000 \
# --oltp-tables-count=24 \
# --threads=1 \
# --mysql-host=mysql-server \
# --mysql-port=3306 \
# --mysql-user=sbtest \
# --mysql-password=password \
# /usr/share/sysbench/tests/include/oltp_legacy/parallel_prepare.lua \
# run

# sysbench \
# --db-driver=mysql \
# --report-interval=2 \
# --mysql-table-engine=innodb \
# --oltp-table-size=100000 \
# --oltp-tables-count=24 \
# --threads=64 \
# --time=99999 \
# --mysql-host=mysql-server \
# --mysql-port=3306 \
# --mysql-user=sbtest \
# --mysql-password=password \
# /usr/share/sysbench/tests/include/oltp_legacy/oltp.lua \
# run