# from pymemcache.client import base
import sys
import os
import time
import subprocess

exp_type = sys.argv[1:][0]

CACHE_MEM_SIZE = 10240
# CACHE_MEM_SIZE = 16
# LITE_SIZE = 5120
LITE_SIZE = 10240
# REPLICA_SIZE = 67
# LITE_SIZE = 40960
# REPLICA_SIZE = 67
REPLICA_SIZE = 50
# LITE_SIZE = 81920
# LITE_SIZE = 256000
# REPLICA_SIZE = 512
# REPLICA_SIZE = 256

os.system(r'pgrep "memcached" | xargs kill -9')
os.system(r'pgrep "LiteMemcached" | xargs kill -9')
os.system(r'pgrep "lite_cli" | xargs kill -9')
time.sleep(1)

if exp_type == 'lite':
  boot_command = ["memcached", "-s", "/tmp/memcached.sock", "-d", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)

  path = os.path.expanduser('~/LiteMemcached')
  boot_command = [path, '-t', '4', '-s', f"{LITE_SIZE}"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
elif exp_type == 'full':
  boot_command = ["memcached", "-d", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4", "-l", "0.0.0.0"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
else:
  print('Invalid experiment type')
  exit(1)
