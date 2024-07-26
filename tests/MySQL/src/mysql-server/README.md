# Build

```sh
apt install -y libaio-dev libncurses5-dev bison
cmake -B=./build -DBUILD_CONFIG=mysql_release -DDOWNLOAD_BOOST=1 -DWITH_BOOST=./build/boost
cd build
make -j `nproc --all`
make install
```

# Init

```sh
mkdir /usr/local/mysql/data
/usr/local/mysql/bin/mysqld --initialize-insecure
/usr/local/mysql/bin/mysqld --user=root --port=60000 --query_cache_type=ON
```

# Setup Sysbench

```sh
/usr/local/mysql/bin/mysql -P 60000 -e "CREATE SCHEMA sbtest;"
/usr/local/mysql/bin/mysql -P 60000 -e "CREATE USER 'sbtest'@'%' IDENTIFIED BY 'password';"
/usr/local/mysql/bin/mysql -P 60000 -e "GRANT ALL PRIVILEGES ON sbtest.* TO 'sbtest'@'%';"
```

# Query Cache Related Stat

```sh
/usr/local/mysql/bin/mysql -P 60000 -e "SHOW VARIABLES LIKE 'have_query_cache';"
/usr/local/mysql/bin/mysql -P 60000 -e "SHOW VARIABLES LIKE 'query_cache_type';"
/usr/local/mysql/bin/mysql -P 60000 -e "SHOW STATUS LIKE 'Qcache%';"
```

# Shutdown

```sh
# /usr/local/mysql/bin/mysqld --user=root shutdown
/usr/local/mysql/bin/mysql -P 60000 -e "SHUTDOWN;"
```