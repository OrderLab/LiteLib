#!/bin/bash

set -e
set -x

# apt -y install software-properties-common
DATABASE_SERVER_IP=${1:-"node2"}
MEMCACHED_SERVER_IP=${2:-"node1"}
DATABASE_QUERY_WEIGHT=${3:-"2000"}

if [ "$(id -u)" != "0" ]; then
  echo "This script must be run as root" 1>&2
  exit 1
fi

if ! command -v php-fpm7.2 >/dev/null ||
   ! command -v nginx >/dev/null ||
   ! php -m | grep -q memcached ||
   ! php -m | grep -q mysqli; then
  apt-get update
  apt-get install -y software-properties-common
  add-apt-repository ppa:ondrej/php -y
  apt-get update
  apt-get install -y \
    php7.2-cli php7.2-fpm php7.2-curl php7.2-gd php7.2-mysql \
    php7.2-mbstring zip unzip php7.2-memcached nginx
fi

if [ -d /etc/nginx/sites-available ]; then
  cp /etc/nginx/sites-available/default /etc/nginx/sites-available/default.bak
  cp ../config_files/default.new /etc/nginx/sites-available/default
else
  cp /etc/nginx/conf.d/default.conf /etc/nginx/conf.d/default.conf.bak
  cp ../config_files/default.new /etc/nginx/conf.d/default.conf
fi
# cp ../config_files/default /etc/nginx/conf.d/default.conf

# curl -sSLo /tmp/debsuryorg-archive-keyring.deb https://packages.sury.org/debsuryorg-archive-keyring.deb
# dpkg -i /tmp/debsuryorg-archive-keyring.deb
# apt update
# apt -y install php7.2-memcache

cp ../NGINX\ Web\ Server/www/html/index.php.template ../NGINX\ Web\ Server/www/html/index.php
sed -i "s/DATABASE_SERVER_IP/$DATABASE_SERVER_IP/"  ../NGINX\ Web\ Server/www/html/index.php
sed -i "s/MEMCACHED_SERVER_IP/$MEMCACHED_SERVER_IP/"  ../NGINX\ Web\ Server/www/html/index.php
sed -i "s/DATABASE_QUERY_WEIGHT/$DATABASE_QUERY_WEIGHT/"  ../NGINX\ Web\ Server/www/html/index.php

cp ../NGINX\ Web\ Server/www/html/util/db_connect_test.php.template ../NGINX\ Web\ Server/www/html/util/db_connect_test.php
sed -i "s/DATABASE_SERVER_IP/$DATABASE_SERVER_IP/" ../NGINX\ Web\ Server/www/html/util/db_connect_test.php

cp ../NGINX\ Web\ Server/www/html/util/memcached_throughput_test.php.template ../NGINX\ Web\ Server/www/html/util/memcached_throughput_test.php
sed -i "s/MEMCACHED_SERVER_IP/$MEMCACHED_SERVER_IP/" ../NGINX\ Web\ Server/www/html/util/memcached_throughput_test.php

#copy all server codes to appropriate folder
# rm -r /var/www/html/*
# mkdir -p /var/www/html/
# cp -r ../NGINX\ Web\ Server/www/html/* /var/www/html/
if [ -d /usr/share/nginx/html ] && [ ! -L /usr/share/nginx/html ]; then
  mv /usr/share/nginx/html /usr/share/nginx/html.bak
fi
ln -sfn "`pwd`/../NGINX Web Server/www/html" /usr/share/nginx/html

# set execution time
# set default execution time 1s
#set default request_termination time 1s
##/etc/php/7.2/fpm/php.ini  max_execution_time
#/etc/php/7.2/fpm/pool.d/www.conf
chmod 777 /etc/php/7.2/fpm/php.ini
chmod 777 /etc/php/7.2/fpm/pool.d/www.conf

cp /etc/php/7.2/fpm/php.ini /etc/php/7.2/fpm/php.ini.bak
sed -i 's/max_execution_time/;max_execution_time/' /etc/php/7.2/fpm/php.ini
echo "max_execution_time = 1" >> /etc/php/7.2/fpm/php.ini

cp /etc/php/7.2/fpm/pool.d/www.conf /etc/php/7.2/fpm/pool.d/www.conf.bak
sed -i 's/request_terminate_timeout/;request_terminate_timeout/' /etc/php/7.2/fpm/pool.d/www.conf
echo "request_terminate_timeout= 1s" >> /etc/php/7.2/fpm/pool.d/www.conf
sed -i 's/pm = dynamic/;pm = dynamic/' /etc/php/7.2/fpm/pool.d/www.conf
echo "pm = static" >> /etc/php/7.2/fpm/pool.d/www.conf
echo "pm.max_children = 32" >> /etc/php/7.2/fpm/pool.d/www.conf

service nginx reload
service php7.2-fpm restart

echo "Done executing setup_server.sh"