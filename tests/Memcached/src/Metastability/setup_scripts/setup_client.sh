#!/bin/bash

set -e
set -x

NGINX_SERVER_IP=${1:-"node0"}
MEMCACHED_SERVER_IP=${2:-"node1"}
MYSQL_SERVER_IP=${3:-"node2"}
DB_ENTRIES=${4:-"1400000"}

if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
else
  SUDO="sudo"
fi

${SUDO} apt update
${SUDO} apt -y install \
  build-essential python3 python3-pip \
  libjpeg8-dev zlib1g-dev mysql-client

pip3 install matplotlib scipy pymemcache SciencePlots

cp ../LoadGenerator/TraceReplay.cpp.template ../LoadGenerator/TraceReplay.cpp
sed -i "/string ngnix_server_ip =/c\string ngnix_server_ip =\"$NGINX_SERVER_IP\";" ../LoadGenerator/TraceReplay.cpp

cp ../LoadGenerator/run_experiment.py.template ../LoadGenerator/run_experiment.py
sed -i "/memcached_host =/c\memcached_host = \'$MEMCACHED_SERVER_IP\'" ../LoadGenerator/run_experiment.py
sed -i "/mysql_host =/c\mysql_host = \'$MYSQL_SERVER_IP\'" ../LoadGenerator/run_experiment.py
sed -i "/row_nums_in_db =/c\row_nums_in_db = $DB_ENTRIES" ../LoadGenerator/run_experiment.py

cp ../LoadGenerator/trigger_size_k.py.template ../LoadGenerator/trigger_size_k.py
sed -i "/memcached_host =/c\memcached_host = \'$MEMCACHED_SERVER_IP\'" ../LoadGenerator/trigger_size_k.py

cp ../LoadGenerator/TraceFileGenerator.py.template ../LoadGenerator/TraceFileGenerator.py
sed -i "/row_nums_in_db =/c\row_nums_in_db = $DB_ENTRIES" ../LoadGenerator/TraceFileGenerator.py

mkdir -p ../LoadGenerator/traces
mkdir -p ../LoadGenerator/result_stats
mkdir -p ../LoadGenerator/experiment_plots
mkdir -p ../LoadGenerator/result_plot
cd ../LoadGenerator && make

ssh-keyscan $NGINX_SERVER_IP  >> $HOME/.ssh/known_hosts
ssh-keyscan $MEMCACHED_SERVER_IP  >> $HOME/.ssh/known_hosts

echo "Done executing setup_client.sh"