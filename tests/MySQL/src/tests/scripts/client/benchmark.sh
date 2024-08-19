#!/bin/bash

sysbench \
/usr/local/share/sysbench/oltp_read_write.lua \
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
/usr/local/share/sysbench/oltp_read_write.lua \
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
--db-ps-mode=disable \
--mysql-ignore-errors=2013,1062 \
--skip_trx=on \
--rand-type=zipfian \
run
