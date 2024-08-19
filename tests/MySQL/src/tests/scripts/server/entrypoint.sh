#!/bin/sh

apt update
apt install -y libaio-dev libncurses5-dev bison

cd /workspace/tests/MySQL/src/mysql-server/
cmake -B=./build -DBUILD_CONFIG=mysql_release -DDOWNLOAD_BOOST=1 -DWITH_BOOST=./build/boost
cd build
make -j `nproc --all`
make install

mkdir /usr/local/mysql/data
/usr/local/mysql/bin/mysqld --initialize-insecure
/usr/local/mysql/bin/mysqld --user=root --port=60000 --query_cache_type=ON

/usr/local/mysql/bin/mysql -P 60000 -e "CREATE SCHEMA sbtest;"
/usr/local/mysql/bin/mysql -P 60000 -e "CREATE USER 'sbtest'@'%' IDENTIFIED BY 'password';"
/usr/local/mysql/bin/mysql -P 60000 -e "GRANT ALL PRIVILEGES ON sbtest.* TO 'sbtest'@'%';"

tail -f /dev/null