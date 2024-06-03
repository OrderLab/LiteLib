import os
import argparse

import monitor
import utils
import crash

parser = argparse.ArgumentParser(description='Init experiment')
parser.add_argument('-t', '--experiment_type', choices=['Full', 'Lite'], required=True, help='The type of the experiment')
parser.add_argument('-s', '--memory_size', type=str, help='The memory limit of the lite version')
args = parser.parse_args()

os.system(r'pgrep "redis-lite" | xargs kill -9')
os.system(r'pgrep "lite_cli" | xargs kill -9')
os.system(r'pgrep "redis-server" | xargs kill -9')
os.system(r'pgrep "redis-sentinel" | xargs kill -9')

os.system(r'find /workspace -name "*.rdb" -type f -delete')

if (args.experiment_type == 'Full'):
    os.system(r'cp /workspace/redis_full.conf /workspace/redis_full_running.conf')
    boot_command = ["redis-server ", "/workspace/redis_full_running.conf"]
    utils.StartBackgroundProcess(boot_command)
    
    os.system(r'cp /workspace/redis_replica.conf /workspace/redis_replica_running.conf')
    boot_command = ["redis-server ", "/workspace/redis_replica_running.conf"]
    utils.StartBackgroundProcess(boot_command)
    
else:
    os.system(r'cp /workspace/redis_full.conf /workspace/redis_full_running.conf')
    boot_command = ["redis-server ", "/workspace/redis_full_running.conf"]
    utils.StartBackgroundProcess(boot_command)
    
    # boot_command = ["/workspace/server/redis-lite", '-s', args.memory_size]
    boot_command = ["/workspace/redis-lite"]
    utils.StartBackgroundProcess(boot_command)
