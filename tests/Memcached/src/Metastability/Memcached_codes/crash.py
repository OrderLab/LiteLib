# from pymemcache.client import base
import sys
import os
import time
import subprocess
import psutil
from sync import sync
import socket

crash_time = int(sys.argv[1:][0])
exp_type = sys.argv[1:][1]
CACHE_MEM_SIZE = 10240

for proc in psutil.process_iter(['pid', 'name']):
  if proc.info['name'] == 'memcached':
    proc.kill()
if os.path.exists("/tmp/memcached.sock"):
  os.remove("/tmp/memcached.sock")
if exp_type == 'lite':
  boot_command = ["~/lite_cli", "-t", "/tmp/lite_memcached", "-p", "/tmp/memcached.sock", "-m", "1"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
print('failure triggered')

time.sleep(crash_time)

if exp_type == 'lite':
  boot_command = ["memcached", "-s", "/tmp/memcached.sock", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)

  boot_command = ["~/lite_cli", "-t", "/tmp/lite_memcached", "-p", "/tmp/memcached.sock", "-m", "0"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
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
  boot_command = ["memcached", "-d", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4", "-l", "0.0.0.0"]
  process = subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
  time.sleep(5)
  # Check if the process has ended
  if process.poll() is not None:
      print(f"The process ended with return code {process.returncode}")
  else:
      print("The process is still running")
else:
  print('Invalid experiment type')
  exit(1)