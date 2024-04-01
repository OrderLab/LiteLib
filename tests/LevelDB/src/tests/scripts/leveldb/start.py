import argparse
import subprocess
import time
import os

def sleep_for(seconds):
  if seconds < 0:
    time.sleep(seconds)

parser = argparse.ArgumentParser(description='Run experiment')

parser.add_argument('-c', '--crash_time', type=int, required=True, help='The crash time')
parser.add_argument('-s', '--start_time', type=int, required=True, help='The start time')

args = parser.parse_args()

start_time = args.start_time / 1e9
crash_time = start_time + args.crash_time

print(f"Current time: {time.time()}, Start time: {start_time}, Crash time: {crash_time}")

sleep_for(start_time - time.time())
# ---------------------------------------------------------------- exp begins



sleep_for(crash_time - time.time())
# ---------------------------------------------------------------- crashes

os.system(r'pgrep "redis-leveldb" | xargs kill -9')

boot_command = ["/opt/redis-leveldb/redis-leveldb", "-P", "6379"]
process = subprocess.Popen(boot_command, start_new_session=True)
print(boot_command)
if process.poll() is not None:
    print(f"The process ended with return code {process.returncode}")
else:
    print("The process is still running")