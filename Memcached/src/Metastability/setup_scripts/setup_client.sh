#!/bin/bash
apt update
apt -y install ssh
service ssh start
cp /workspace/.ssh /root -r
chmod 600 /root/.ssh/id_ed25519
apt -y install libjpeg8-dev zlib1g-dev curl
NGINX_SERVER_IP=${1:-"web"}
MEMCACHED_SERVER_IP=${2:-"memcached"}
DB_ENTRIES=${3:-"140000"}
MASTER_VM=${4:-"client"}
apt-get update;
apt -y install build-essential;
apt-get -y install python3-pip;
pip3 install matplotlib
pip3 install scipy
pip3 install pymemcache
pip3 install SciencePlots
apt-get install sshpass
sed -i "/string ngnix_server_ip =/c\string ngnix_server_ip =\"$NGINX_SERVER_IP\";" ../LoadGenerator/TraceReplay.cpp
sed -i "/memcached_host =/c\memcached_host = \'$MEMCACHED_SERVER_IP\'" ../LoadGenerator/run_experiment.py 
sed -i "/master_vm =/c\master_vm = \'$MASTER_VM\'" ../LoadGenerator/run_experiment.py 

sed -i "/memcached_host =/c\memcached_host = \'$MEMCACHED_SERVER_IP\'" ../LoadGenerator/trigger_size_k.py
sed -i "/row_nums_in_db =/c\row_nums_in_db = $DB_ENTRIES" ../LoadGenerator/TraceFileGenerator.py
mkdir ../LoadGenerator/traces
mkdir ../LoadGenerator/results_warm_cache
mkdir ../LoadGenerator/result_stats 
mkdir ../LoadGenerator/experiment_plots
cd ../LoadGenerator && make
#chmod 600 ../config_files/cache_workers.pem
#cp ../config_files/cache_workers.pem ../LoadGenerator/cache_workers.pem

ssh-keyscan $NGINX_SERVER_IP  >> $HOME/.ssh/known_hosts
ssh-keyscan $MEMCACHED_SERVER_IP  >> $HOME/.ssh/known_hosts 
echo "Done executing setup_client.sh"