#!/bin/bash

set -e
set -x

NGINX_SERVER_IP=${1:-"node0"}
MEMCACHED_SERVER_IP=${2:-"node1"}
DB_ENTRIES=${3:-"140000"}

if [ "$(id -u)" -eq 0 ]; then
  echo "This script should not be run as root. Please run as a regular user."
  exit 1
fi

sudo apt -y install libjpeg8-dev zlib1g-dev

pip3 install matplotlib scipy pymemcache SciencePlots

cp ../LoadGenerator/TraceReplay.cpp.template ../LoadGenerator/TraceReplay.cpp
sed -i "/string ngnix_server_ip =/c\string ngnix_server_ip =\"$NGINX_SERVER_IP\";" ../LoadGenerator/TraceReplay.cpp

cp ../LoadGenerator/run_experiment.py.template ../LoadGenerator/run_experiment.py
sed -i "/memcached_host =/c\memcached_host = \'$MEMCACHED_SERVER_IP\'" ../LoadGenerator/run_experiment.py

cp ../LoadGenerator/trigger_size_k.py.template ../LoadGenerator/trigger_size_k.py
sed -i "/memcached_host =/c\memcached_host = \'$MEMCACHED_SERVER_IP\'" ../LoadGenerator/trigger_size_k.py

cp ../LoadGenerator/TraceFileGenerator.py.template ../LoadGenerator/TraceFileGenerator.py
sed -i "/row_nums_in_db =/c\row_nums_in_db = $DB_ENTRIES" ../LoadGenerator/TraceFileGenerator.py

mkdir ../LoadGenerator/traces
mkdir ../LoadGenerator/result_stats
mkdir ../LoadGenerator/experiment_plots
cd ../LoadGenerator && make

ssh-keyscan $NGINX_SERVER_IP  >> $HOME/.ssh/known_hosts
ssh-keyscan $MEMCACHED_SERVER_IP  >> $HOME/.ssh/known_hosts

echo "Done executing setup_client.sh"