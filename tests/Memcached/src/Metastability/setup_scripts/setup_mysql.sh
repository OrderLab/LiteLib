#!/bin/bash

set -e
set -x

NGINX_SERVER_IP=${1:-"%"}
# DB_ENTRIES=${2:-"1504000"}
DB_ENTRIES=${2:-"34600000"}
MYSQL_ROOT_PASSWORD=${MYSQL_ROOT_PASSWORD:-"hello@123"}

if [ "$(id -u)" != "0" ]; then
  echo "This script must be run as root" 1>&2
  exit 1
fi

apt install -y mysql-server mysql-client

echo "bind-address = 0.0.0.0" >> /etc/mysql/mysql.conf.d/mysqld.cnf

if command -v ufw >/dev/null 2>&1; then
  ufw allow 3306
fi
service mysql restart

cat > /root/.my.cnf <<EOF
[client]
user=root
password=${MYSQL_ROOT_PASSWORD}
EOF
chmod 600 /root/.my.cnf

# Route every mysql invocation in this script through the credentials file,
# including legacy calls below that still spell out `-u root`.
mysql() {
  command mysql --defaults-extra-file=/root/.my.cnf "$@"
}

# adding webserver IP
cp add_user.sql.template add_user.sql
sed -i "s/remote_server_ip/$NGINX_SERVER_IP/" add_user.sql
mysql < add_user.sql

cp init_database.sql.template init_database.sql
sed -i "s/remote_server_ip/$NGINX_SERVER_IP/" init_database.sql
mysql < init_database.sql

# touch new_user.sql
# chmod 777 new_user.sql
# echo "CREATE USER 'metastable'@'%' IDENTIFIED BY 'hello@123';" > new_user.sql
# echo "GRANT CREATE, ALTER, DROP, INSERT, UPDATE, DELETE, SELECT, REFERENCES, RELOAD on *.* TO 'metastable'@'%' WITH GRANT OPTION;" >> new_user.sql
# echo "ALTER USER 'metastable'@'%' IDENTIFIED WITH mysql_native_password BY 'hello@123';" >> new_user.sql
# echo "FLUSH PRIVILEGES;" >> new_user.sql
# mysql -u root -phello@123 < new_user.sql

# wget https://github.com/Percona-Lab/mysql_random_data_load/releases/download/v0.1.12/mysql_random_data_load_0.1.12_Linux_x86_64.tar.gz
# tar -xvf  mysql_random_data_load_*
mysql -u root -e "ALTER USER 'root'@'localhost' IDENTIFIED WITH caching_sha2_password BY 'hello@123';;FLUSH PRIVILEGES;"

for i in {1..10}; do
    ./mysql_random_data_load metastable_test_db large_test_table $(($DB_ENTRIES/10)) --user=root --password=hello@123 &
done

while pgrep -f mysql_random_data_load > /dev/null; do
    echo "Waiting for mysql_random_data_load processes to complete..."
    sleep 5
done

cp linearize_column_data.sql.template linearize_column_data.sql
sed -i "/SET @a:=/c\SET @a:= $DB_ENTRIES;" linearize_column_data.sql
mysql < linearize_column_data.sql # takes about 1 hour

echo "Done executing setup_mysql.sh"
