# from pymemcache.client import base
import sys
import os
import time
import subprocess

exp_type = sys.argv[1:][0]

CACHE_MEM_SIZE = 4096000
# CACHE_MEM_SIZE = 16
# LITE_SIZE = 2560
# LITE_SIZE = 10240
# REPLICA_SIZE = 67
LITE_SIZE = 40960
# REPLICA_SIZE = 67
# REPLICA_SIZE = 50
# LITE_SIZE = 3276800
# LITE_SIZE = 256000
# REPLICA_SIZE = 512
# REPLICA_SIZE = 256

def StartBackgroundProcess(boot_command, log_file, append=False, env=dict()):
  print(boot_command)
  print(log_file)
  log = None
  if append:
    log = open(log_file, "a+")
  else:
    log = open(log_file, "w+")
  process = subprocess.Popen(
    boot_command,
    stdout=log,
    stderr=log,
    start_new_session=True,
    env=dict(os.environ) | env,
  )
  if process.poll() is not None:
    print(f"The process ended with return code {process.returncode}")
    exit(1)
  else:
    print("The process is still running")
  return process

os.system(r'pgrep "memcached" | xargs kill -9')
os.system(r'pgrep "LiteMemcached" | xargs kill -9')
os.system(r'pgrep "lite_cli" | xargs kill -9')
time.sleep(1)

if exp_type == 'lite':
  boot_command = ["memcached", "-s", "/tmp/memcached.sock", "-d", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "32"]
  StartBackgroundProcess(boot_command, "/tmp/memcached.log")

  path = os.path.expanduser('~/LiteMemcached')
  boot_command = [path, '-t', '4', '-s', f"{LITE_SIZE}"]
  StartBackgroundProcess(boot_command, "/tmp/lite_memcached.log", env={"GLOG_stderrthreshold": "0", "GLOG_logtostderr": "1"})
elif exp_type == 'full':
  boot_command = ["memcached", "-d", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "32", "-l", "0.0.0.0"]
  StartBackgroundProcess(boot_command, "/tmp/memcached.log")
else:
  print('Invalid experiment type')
  exit(1)
