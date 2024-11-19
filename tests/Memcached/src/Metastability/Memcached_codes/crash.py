# from pymemcache.client import base
import sys
import os
import time
import subprocess
import psutil
from sync import sync
import socket

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

crash_time = int(sys.argv[1:][0])
exp_type = sys.argv[1:][1]
CACHE_MEM_SIZE = 4096000

for proc in psutil.process_iter(['pid', 'name']):
  if proc.info['name'] == 'memcached':
    proc.kill()
if os.path.exists("/tmp/memcached.sock"):
  os.remove("/tmp/memcached.sock")
if exp_type == 'lite':
  path = os.path.expanduser('~/lite_cli')
  boot_command = [path, "-t", "/tmp/lite_memcached", "-p", "/tmp/memcached.sock", "-m", "1"]
  StartBackgroundProcess(boot_command, "/tmp/lite_cli-1.log")
print('failure triggered')

time.sleep(crash_time)

if exp_type == 'lite':
  boot_command = ["memcached", "-s", "/tmp/memcached.sock", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "32"]
  StartBackgroundProcess(boot_command, "/tmp/memcached.log", True)

  path = os.path.expanduser('~/lite_cli')
  boot_command = [path, "-t", "/tmp/lite_memcached", "-p", "/tmp/memcached.sock", "-m", "0"]
  StartBackgroundProcess(boot_command, "/tmp/lite_cli-2.log")
elif exp_type == 'full':
  # Wait until port 11211 is available
  while True:
      sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      try:
          sock.bind(('0.0.0.0', 11211))
          sock.close()
          break
      except socket.error:
          continue
  boot_command = ["memcached", "-d", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "32", "-l", "0.0.0.0"]
  StartBackgroundProcess(boot_command, "/tmp/memcached.log", True)
else:
  print('Invalid experiment type')
  exit(1)
