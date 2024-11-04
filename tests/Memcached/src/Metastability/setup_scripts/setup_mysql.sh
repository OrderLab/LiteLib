#!/bin/bash

set -e
set -x

NGINX_SERVER_IP=${1:-"node0"}
DB_ENTRIES=${2:-"1504000"}

if [ "$(id -u)" != "0" ]; then
  echo "This script must be run as root" 1>&2
  exit 1
fi

apt install -y mysql-server mysql-client

echo "bind-address = 0.0.0.0" >> /etc/mysql/mysql.conf.d/mysqld.cnf

ufw allow 3306
service mysql restart

# adding webserver IP
cp add_user.sql.template add_user.sql
sed -i "s/remote_server_ip/$NGINX_SERVER_IP/" add_user.sql
mysql -u root < add_user.sql

cp init_database.sql.template init_database.sql
sed -i "s/remote_server_ip/$NGINX_SERVER_IP/" init_database.sql
mysql -u root < init_database.sql

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
./mysql_random_data_load metastable_test_db large_test_table $DB_ENTRIES --user=root --password=hello@123

sed -i "/SET @a:=/c\SET @a:= $DB_ENTRIES;" linearize_column_data.sql
mysql -u root < linearize_column_data.sql

echo "Done executing setup_mysql.sh"
