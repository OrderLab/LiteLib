import os
import argparse

import utils
import time

parser = argparse.ArgumentParser(description='Init experiment')
parser.add_argument('--mode', choices=['replica', 'lite'], required=True, help='The type of the experiment')
parser.add_argument('--crashtime', type=int, default=0, help='The time to crash the server')
args = parser.parse_args()

os.system(r'pgrep "redis-lite" | xargs kill -9')
os.system(r'pgrep "lite_cli" | xargs kill -9')
os.system(r'pgrep "redis-server" | xargs kill -9')
os.system(r'pgrep "redis-sentinel" | xargs kill -9')

os.system(r'find /workspace -name "*.rdb" -type f -delete')

if (args.mode == 'replica'):
    os.system(r'cp /workspace/redis_full.conf /workspace/redis_full_running.conf')
    boot_command = ["redis-server", "/workspace/redis_full_running.conf"]
    utils.StartBackgroundProcess(boot_command)

    os.system(r'cp /workspace/redis_replica.conf /workspace/redis_replica_running.conf')
    boot_command = ["redis-server", "/workspace/redis_replica_running.conf"]
    utils.StartBackgroundProcess(boot_command)

    # Check if both servers are booted
    if utils.IsProcessRunning("redis-server") and utils.IsProcessRunning("redis-sentinel"):
        monitor_command = ["python3", "/workspace/experiment/monitor.py", "--mode", "replica", "--duration", "120"]
        utils.StartBackgroundProcess(monitor_command)
        
        bench_command = ["python3", "/workspace/experiment/omni_bench.py", "--mode", "lite", "-t", "10", "-p", "6479", "-n", "2000", "--commands", "HSET", "LPUSH", "SADD", "ZADD"]
        utils.StartBackgroundProcess(bench_command)
        
else:
    os.system(r'cp /workspace/redis_full.conf /workspace/redis_full_running.conf')
    boot_command = ["redis-server", "/workspace/redis_full_running.conf"]
    utils.StartBackgroundProcess(boot_command)
    
    boot_command = ["/workspace/lite-version/build/redis-lite"]
    utils.StartBackgroundProcess(boot_command)
    
    if utils.IsProcessRunning("redis-server") and utils.IsProcessRunning("redis-lite"):
        monitor_command = ["python3", "/workspace/experiment/monitor.py", "--mode", "lite", "--duration", "120"]
        utils.StartBackgroundProcess(monitor_command)
        
        bench_command = ["python3", "/workspace/experiment/omni_bench.py", "--mode", "lite", "-t", "10", "-p", "6479", "-n", "2000", "--commands", "HSET", "LPUSH", "SADD", "ZADD"]
        utils.StartBackgroundProcess(bench_command)
        
        start_time = time.time()
        time.sleep(args.crashtime)
        
        os.system(r'pgrep "redis-server" | xargs kill -2')
        time.sleep(1)
        boot_command = ["/workspace/lite-version/build/Lite/lite_cli", "-t", "/tmp/lite_Redis", "-p", "6379", "-m", "1"]
        utils.StartBackgroundProcess(boot_command)
        boot_command = ["redis-server", "/workspace/redis_full_running.conf"]
        utils.StartBackgroundProcess(boot_command)
        boot_command = ["/workspace/lite-version/build/Lite/lite_cli", "-t", "/tmp/lite_Redis", "-p", "6379", "-m", "0"]
        utils.StartBackgroundProcess(boot_command)
