#!/bin/bash

sysbench \
/usr/share/sysbench/oltp_read_write.lua \
--db-driver=mysql \
--tables=1 \
--table-size=1000000 \
--threads=1 \
--mysql-host=mysql-server \
--mysql-port=60000 \
--mysql-user=sbtest \
--mysql-password=password \
prepare

sysbench \
/usr/share/sysbench/oltp_read_write.lua \
--db-driver=mysql \
--report-interval=1 \
--tables=1 \
--table-size=1000000 \
--threads=1 \
--time=10 \
--mysql-host=mysql-server \
--mysql-port=59999 \
--mysql-user=sbtest \
--mysql-password=password \
--db-ps-mode=disable \
--mysql-ignore-errors=2013,1062 \
--skip_trx=on \
run
