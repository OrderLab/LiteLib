#!/bin/bash
apt update
apt -y install ssh
service ssh start
cp /workspace/.ssh /root -r
chmod 600 /root/.ssh/id_ed25519
# apt -y install software-properties-common
DATABASE_SERVER_IP=${1:-"mysql"}
MEMCACHED_SERVER_IP=${2:-"10.0.233.7"}
DATABASE_QUERY_WEIGHT=${3:-"5000"}
# apt-get update
# apt-get -y upgrade
# apt-get -y install nginx
apt update
apt -y install php7.2-memcache

# add-apt-repository ppa:ondrej/php -y
# yes | apt-get install php7.2-cli php7.2-fpm php7.2-curl php7.2-gd php7.2-mysql php7.2-mbstring zip unzip php7.2-memcache;
#  cp ../config_files/default /etc/nginx/sites-available/default;
 cp ../config_files/default /etc/nginx/conf.d/default.conf


sed -i "s/DATABASE_SERVER_IP/$DATABASE_SERVER_IP/"  ../NGINX\ Web\ Server/www/html/index.php
sed -i "s/MEMCACHED_SERVER_IP/$MEMCACHED_SERVER_IP/"  ../NGINX\ Web\ Server/www/html/index.php
sed -i "s/DATABASE_QUERY_WEIGHT/$DATABASE_QUERY_WEIGHT/"  ../NGINX\ Web\ Server/www/html/index.php
 

sed -i "s/DATABASE_SERVER_IP/$DATABASE_SERVER_IP/" ../NGINX\ Web\ Server/www/html/util/db_connect_test.php
sed -i "s/MEMCACHED_SERVER_IP/$MEMCACHED_SERVER_IP/" ../NGINX\ Web\ Server/www/html/util/memcached_throughput_test.php

#copy all server codes to appropriate folder
# rm -r /var/www/html/*
# mkdir -p /var/www/html/
# cp -r ../NGINX\ Web\ Server/www/html/* /var/www/html/
mv /usr/share/nginx/html /usr/share/nginx/html.bak
ln -s "`pwd`/../NGINX Web Server/www/html" /usr/share/nginx/html

# set execution time
# set default execution time 1s
#set default request_termination time 1s
##/etc/php/7.2/fpm/php.ini  max_execution_time
#/etc/php/7.2/fpm/pool.d/www.conf
chmod 777 /etc/php/7.2/fpm/php.ini
chmod 777 /etc/php/7.2/fpm/pool.d/www.conf

sed -i 's/max_execution-time/;max_execution_time/' /etc/php/7.2/fpm/php.ini
echo "max_execution-time = 1s" >> /etc/php/7.2/fpm/php.ini

sed -i 's/request_terminate_timeout/;request_terminate_timeout/' /etc/php/7.2/fpm/pool.d/www.conf
echo "request_terminate_timeout= 1s" >> /etc/php/7.2/fpm/pool.d/www.conf

service nginx reload
service php7.2-fpm restart

ssh-keyscan $DATABASE_SERVER_IP  >> $HOME/.ssh/known_hosts
ssh-keyscan $MEMCACHED_SERVER_IP  >> $HOME/.ssh/known_hosts 
echo "Done executing setup_server.sh"