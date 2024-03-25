## Yi Chen's Note

### Bugs now

- Can't change time out

### Setup (about 10 min)

```sh
docker compose up -d

# the following 4 can be run in parallel

docker exec -it mysql bash
cd /workspace/setup_scripts; ./setup_mysql.sh

docker exec -it memcached bash
cd /workspace/setup_scripts; ./setup_memcached.sh
# service ssh start
# memcached -d -u root -l 0.0.0.0 -m 1024

docker exec -it web bash
cd /workspace/setup_scripts; ./setup_server.sh
# service nginx reload
# service php7.2-fpm restart

docker exec -it client bash
cd /workspace/setup_scripts; ./setup_client.sh
# service ssh start
```

### Run

Note: You need to modify `$version` in `NGINX Web Server/www/html/index.php` and restart php-fpm when changing the method you want to evaluate (it's the same procedure as changing the query weight, plz refer to the Config section)

```sh
docker exec -it client bash
cd /workspace/LoadGenerator
curl web?request_index=1
python3 run_experiment.py 1500 0 300 1.00001 12 10 False 1 full
# cp ./result_stats/monitor_1500.0_1.00001_DUR_300_TRIGGER_0_TMOUT_1.txt ../Memcached_codes/run1.monitor.txt
# cp ./results_warm_cache/result_1500.0_1.00001_DUR_300_TRIGGER_0_TMOUT_1.txt ../Memcached_codes/run1.result.txt
python3 run_experiment.py 1500 0 300 1.00001 12 10 False 1 new_lite
python3 run_experiment.py 1500 0 300 1.00001 12 10 False 1 lite
# cp ./result_stats/monitor_1500.0_1.00001_DUR_300_TRIGGER_0_TMOUT_1.txt ../Memcached_codes/run2.monitor.txt
# cp ./results_warm_cache/result_1500.0_1.00001_DUR_300_TRIGGER_0_TMOUT_1.txt ../Memcached_codes/run2.result.txt
python3 run_experiment.py 1500 0 300 1.00001 12 10 False 1 replica
# cp ./result_stats/monitor_1500.0_1.00001_DUR_300_TRIGGER_0_TMOUT_1.txt ../Memcached_codes/run3.monitor.txt
# cp ./results_warm_cache/result_1500.0_1.00001_DUR_300_TRIGGER_0_TMOUT_1.txt ../Memcached_codes/run3.result.txt
cd ../Memcached_codes
python3 plot.py 1500 300 5 3 run1 run2 run3
```

### Config

#### Requests

* The number of possible indexes

    Global replace 140000

    Node: Don't forget to delete ./LoadGenerator/traces after modifying them

#### MySQL

* number of entries in the database

```bash
cd setup_scripts
./mysql_random_data_load metastable_test_db large_test_table $DB_ENTRIES --user=root --password=hello@123
sed -i "/SET @a:=/c\SET @a:= $DB_ENTRIES;" linearize_column_data.sql
mysql -u root -phello@123 < linearize_column_data.sql
```

* query weight

    1. modify index.php
    2. run `service php7.2-fpm restart` in docker 'web'
    3. run `curl web?request_index=1` in docker 'client'

* concurrency

```bash
mysql -u root -phello@123 < 'SET GLOBAL innodb_thread_concurrency = 1;'
```

#### Lite Version

* Resource Consumption

    Modify Memcached_codes/init.py


## Summary

This directory contains the programs needed to cause metastability issue in the popularly used Lookaside Cache based systems. For more details on how the metastability issue is triggered, please read section 5.3 of our paper.  


## System Setup

We need 4 VMs / Servers to run this experiment for the following parts:-

1. Load Balancer 
2. Web Server
3. MySQL Server
4. Memcached Server 


Please run the following commands(with appropriate parameters) in each of the VMs to configure & install necessary packages.  

Important Parameters: 
1. WebServer Params:
    CACHE_IP, DATABASE_QUERY_WEIGHT, SQL_IP
2. SqlServer Params:
    WEB_IP, DB_ENTRIES

3. LoadGenerator VM:
    CACHE_IP, MAIN_VM_IP

4. Memcached Params:
    CACHE_WARMUP_SIZE, CACHE_MEM_SIZE

Web Server VM:
<pre> sudo apt-get update && git clone https://github.com/SalmanEstyak/Metastability && cd Metastability &&  cd setup_scripts && sudo chmod +x setup_server.sh && ./setup_server.sh {SQL_IP} {CACHE_IP} {DATABASE_QUERY_WEIGHT} </pre>

SQL Server VM:
 <pre> sudo apt-get install git && sudo git clone https://github.com/SalmanEstyak/Metastability.git && cd Metastability && cd setup_scripts &&  sudo chmod +x setup_mysql.sh && ./setup_mysql.sh {WEB_IP} {DB_ENTRIES}"</pre>

Memcached Server VM:
<pre> sudo apt-get update && sudo apt-get install git  &&  sudo  git clone https://github.com/SalmanEstyak/Metastability.git && cd Metastability && cd setup_scripts &&  sudo chmod +x setup_memcached.sh && ./setup_memcached.sh {CACHE_MEM_SIZE} {CACHE_WARMUP_SIZE}"</pre>

Load Generator VM:
<pre>sudo apt-get update && git clone https://github.com/SalmanEstyak/Metastability.git && cd Metastability && cd setup_scripts && sudo chmod +x setup_client.sh && ./setup_client.sh {WEB_IP} {CACHE_IP} {DB_ENTRIES} {MAIN_VM_IP}"</pre>


After the VMs are setup and every VM has proper IPs to communicate with (the current implement assumes that all the VMs would share a single key to communicate). We can begin running experiments.


## Running Experiments

In the Load Generator VM, run the following command:

<pre>sudo python3 run_experiment.py load trigger duration_of_test zipf_parameter num_threads sleep_period_before_trigger timeout test_type </pre>

Explanation of each parameter:

1. load: Requests Per Second 
2. trigger: Trigger indicates a certain drop in cache hit rate. With -1 as the trigger, all cache entries are dropped.
3. duration_of_test: Experiment run time 
4. zipf_parameter: This controls the job popularity distribution 
5. num_threads: Number of threads to be used in TraceReplay (e.g. value: 64, 128)
6. sleep_period_before_trigger (This dictates the timepoint where the trigger is applied)
7. timeout: This is the maximum time a request can run before it gets killed by the server.  
8. test_type: We can provide any name here. 

## Contact 
Please reach out to me salman.estyak@psu.edu for any issues. 
