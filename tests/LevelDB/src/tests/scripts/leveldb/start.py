import argparse
import time
import os
import utils

def sleep_for(seconds):
  if seconds > 0:
    time.sleep(seconds)

parser = argparse.ArgumentParser(description='Run experiment')
parser.add_argument('-c', '--crash_time', type=int, required=True, help='The crash time')
parser.add_argument('-s', '--start_time', type=int, required=True, help='The start time')
parser.add_argument('-t', '--experiment_type', choices=['Full', 'Lite'], required=True, help='The type of the experiment')
args = parser.parse_args()

start_time = args.start_time / 1e9
crash_time = start_time + args.crash_time

print(f"Current time: {time.time()}, Start time: {start_time}, Crash time: {crash_time}")

sleep_for(start_time - time.time())
# ---------------------------------------------------------------- exp begins



sleep_for(crash_time - time.time())
# ---------------------------------------------------------------- crashes

os.system(r'pgrep "redis-leveldb" | xargs kill -9')

if args.experiment_type == 'Full':
  boot_command = ["/workspace/redis-leveldb/redis-leveldb", "-P", "6379"]
  utils.StartBackgroundProcess(boot_command)
else:
  boot_command = ["/workspace/server/lite_cli", "-t", "/tmp/lite_LevelDB", "-p", "60001", "-m", "1"]
  utils.StartBackgroundProcess(boot_command)

  boot_command = ["/workspace/redis-leveldb/redis-leveldb", "-P", "60001"]
  utils.StartBackgroundProcess(boot_command)

  # TODO: how to know if redis-leveldb is initialized?
  boot_command = ["/workspace/server/lite_cli", "-t", "/tmp/lite_LevelDB", "-p", "60001", "-m", "0"]
  utils.StartBackgroundProcess(boot_command)