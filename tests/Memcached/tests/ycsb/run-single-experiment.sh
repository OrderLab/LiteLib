#!/bin/bash

# run in vanilla node

set -x

SCRIPT_DIR="/home/rishika/cascade/tests/Memcached/tests/ycsb"
YCSB_DIR="/home/rishika/YCSB"
MASTER_HOST="node3"
CLIENT_HOST="node2"
# take arg for mode: lite, replica
MODE=$1
ORIGINAL_SUFFIX=$2
SUFFIX=${MODE}_${ORIGINAL_SUFFIX}

mkdir -p $SCRIPT_DIR/logs

if [ "$MODE" == "vanilla" ] || [ "$MODE" == "embedded" ] || [ "$MODE" == "proxy" ] || [ "$MODE" == "ebpf" ]; then
  echo "Starting Memcached"
  $SCRIPT_DIR/start-$MODE.sh $SUFFIX
else
  echo "Invalid mode"
  exit 1
fi

# Copy workload file to client host
scp $SCRIPT_DIR/memcached_workload $CLIENT_HOST:$YCSB_DIR/workloads/memcached_workload

echo "`date '+%Y-%m-%d %H:%M:%S'` Starting YCSB load"
ssh $CLIENT_HOST "cd $YCSB_DIR; ./bin/ycsb load memcached -s -P workloads/memcached_workload -p memcached.hosts=$MASTER_HOST" > $SCRIPT_DIR/logs/benchmark-$SUFFIX.log 2>&1
echo "`date '+%Y-%m-%d %H:%M:%S'` YCSB load completed"
sleep 20

# Start relevant monitoring processes
python -u $SCRIPT_DIR/monitor.py 120 $SCRIPT_DIR/logs/monitor-$SUFFIX.log 0 &
echo "Monitoring started"

TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
echo "[$TIMESTAMP] Starting benchmark" >> $SCRIPT_DIR/logs/benchmark-$SUFFIX.log

ssh $CLIENT_HOST "cd $YCSB_DIR; ./bin/ycsb run memcached -s -P workloads/memcached_workload -p memcached.hosts=$MASTER_HOST" >> $SCRIPT_DIR/logs/benchmark-$SUFFIX.log 2>&1
STATUS=$?

echo "Benchmark completed with status $STATUS"