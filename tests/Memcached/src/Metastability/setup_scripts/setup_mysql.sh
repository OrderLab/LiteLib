#!/bin/bash
apt update
apt -y install ssh
service ssh start
cp /workspace/.ssh /root -r
chmod 600 /root/.ssh/id_ed25519
NGINX_SERVER_IP=${1:-"web"}
DB_ENTRIES=${2:-"19741000"}
# echo "params: $NGINX_SERVER_IP $DB_ENTRIES" 
# chmod 777  /var/cache/debconf/config.dat
# cat ../config_files/deb_conf.dat >>  /var/cache/debconf/config.dat
# wget http://repo.mysql.com/mysql-apt-config_0.8.10-1_all.deb
# DEBIAN_FRONTEND=noninteractive dpkg -i mysql-apt-config_0.8.10-1_all.deb
# apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 467B942D3A79BD29
# apt-get update 
# apt update
# apt install -y mysql-client=5.7.*-1ubuntu18.04
# debconf-set-selections <<< 'mysql-community-server mysql-community-server/root-pass password hello@123'
# debconf-set-selections <<< 'mysql-community-server mysql-community-server/re-root-pass password hello@123'
# DEBIAN_FRONTEND=noninteractive apt install -y mysql-community-server=5.7.*-1ubuntu18.04
# DEBIAN_FRONTEND=noninteractive apt install -y mysql-server=5.7.*-1ubuntu18.04
# mysql -u root -phello@123 < init_database.sql

# sed -i "s/.*bind-address.*/bind-address = 0.0.0.0/" /etc/mysql/mysql.conf.d/mysqld.cnf
# /etc/init.d/mysql stop
# /etc/init.d/mysql start

# wget https://github.com/Percona-Lab/mysql_random_data_load/releases/download/v0.1.12/mysql_random_data_load_0.1.12_Linux_x86_64.tar.gz
# tar -xvf  mysql_random_data_load_*
# ./mysql_random_data_load metastable_test_db large_test_table $DB_ENTRIES  --user=root --password=hello@123
# wget random data generator , run it with params
# construct a mini sql file from here
# for adding server IP, user to mysql

# adding webserver IP
# sed -i "/SET @a:=/c\SET @a:= $DB_ENTRIES;" linearize_column_data.sql
# sed -i "s/remote_server_ip/$NGINX_SERVER_IP/" add_user.sql
# ufw allow 3306
# mysql -u root -phello@123 < add_user.sql
# mysql -u root -phello@123 < linearize_column_data.sql

# echo "params: $NGINX_SERVER_IP $DB_ENTRIES" 
#chmod 777  /var/cache/debconf/config.dat
#cat ../config_files/deb_conf.dat >>  /var/cache/debconf/config.dat
#wget http://repo.mysql.com/mysql-apt-config_0.8.10-1_all.deb
#DEBIAN_FRONTEND=noninteractive dpkg -i mysql-apt-config_0.8.10-1_all.deb
#apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 467B942D3A79BD29
#apt-get update 
#apt update
#apt install -y mysql-client=5.7.*-1ubuntu18.04
#debconf-set-selections <<< 'mysql-community-server mysql-community-server/root-pass password hello@123'
#debconf-set-selections <<< 'mysql-community-server mysql-community-server/re-root-pass password hello@123'
#DEBIAN_FRONTEND=noninteractive apt install -y mysql-community-server=5.7.*-1ubuntu18.04
#DEBIAN_FRONTEND=noninteractive apt install -y mysql-server=5.7.*-1ubuntu18.04
#mysql -u root -phello@123 < init_database.sql

#sed -i "s/.*bind-address.*/bind-address = 0.0.0.0/" /etc/mysql/mysql.conf.d/mysqld.cnf
#/etc/init.d/mysql stop
#/etc/init.d/mysql start

#wget https://github.com/Percona-Lab/mysql_random_data_load/releases/download/v0.1.12/mysql_random_data_load_0.1.12_Linux_x86_64.tar.gz
#tar -xvf  mysql_random_data_load_*
#./mysql_random_data_load metastable_test_db large_test_table $DB_ENTRIES  --user=root --password=hello@123
# wget random data generator , run it with params
# construct a mini sql file from here
# for adding server IP, user to mysql

# adding webserver IP
#sed -i "/SET @a:=/c\SET @a:= $DB_ENTRIES;" linearize_column_data.sql

#mysql -u root -phello@123 < linearize_column_data.sql
 
touch new_user.sql
chmod 777 new_user.sql
echo "CREATE USER 'metastable'@'%' IDENTIFIED BY 'hello@123';" > new_user.sql
echo "GRANT CREATE, ALTER, DROP, INSERT, UPDATE, DELETE, SELECT, REFERENCES, RELOAD on *.* TO 'metastable'@'%' WITH GRANT OPTION;" >> new_user.sql
echo "ALTER USER 'metastable'@'%' IDENTIFIED WITH mysql_native_password BY 'hello@123';" >> new_user.sql
echo "FLUSH PRIVILEGES;" >> new_user.sql 
 
#ufw allow 3306
mysql -u root -phello@123 < new_user.sql

mysql -u root -phello@123 < init_database.sql

# wget https://github.com/Percona-Lab/mysql_random_data_load/releases/download/v0.1.12/mysql_random_data_load_0.1.12_Linux_x86_64.tar.gz
# tar -xvf  mysql_random_data_load_*
./mysql_random_data_load metastable_test_db large_test_table $DB_ENTRIES --user=root --password=hello@123

sed -i "/SET @a:=/c\SET @a:= $DB_ENTRIES;" linearize_column_data.sql
mysql -u root -phello@123 < linearize_column_data.sql

echo "Done executing setup_mysql.sh"
