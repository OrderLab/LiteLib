# from pymemcache.client import base
import sys
import os
import time
import subprocess
import psutil
from sync import sync

crash_time = int(sys.argv[1:][0])
exp_type = sys.argv[1:][1]
CACHE_MEM_SIZE = 10240

# kill_command = r'pgrep -x "memcached" | xargs kill -9'
# os.system(kill_command)
# subprocess.run(["bash", "-c", f"'{kill_command}'"], shell=True, timeout=10)
# subprocess.run(kill_command, shell=True, timeout=10)
for proc in psutil.process_iter(['pid', 'name']):
  if proc.info['name'] == 'memcached':
    proc.kill()
print('failure triggered')

time.sleep(crash_time)

if exp_type == 'new_lite':
  boot_command = ["/workspace/Memcached_codes/lite_cli", "-t", "/tmp/lite_memcached", "-p", "60001", "-m", "1"]
  # /workspace/Memcached_codes/lite_cli -t /tmp/lite_memcached -p 60001 -m 1
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)

  boot_command = ["memcached", "-p", "60001", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)

  sync(exp_type)
elif exp_type == 'lite' or exp_type == 'replica':
  os.system("""
  echo "
  -d -t 10.0.233.7:11211 -r 192.168.254.10:60000
  -e -t 10.0.233.7:11211 -r 192.168.254.10:60001 -m -w 10
  " | ipvsadm -R
  """)
  os.system(r'ipvsadm -l')

  boot_command = ["ip", "net", "e", "testx", "memcached", "-p", "60002", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)

  sync(exp_type)

  os.system(r'ipvsadm -C') # if the client doesn't use persistent connections, then there's no need to remove the backup from the pool
  os.system(r'ipvsadm -l')
  os.system("""
  echo "
  -A -t 10.0.233.7:11211 -s rr
  -a -t 10.0.233.7:11211 -r 192.168.254.10:60002 -m -w 10
  -a -t 10.0.233.7:11211 -r 192.168.254.10:60001 -m -w 0
  " | ipvsadm -R""")
  os.system("""
  echo "
  -A -t 10.0.233.7:59999 -s rr
  -a -t 10.0.233.7:59999 -r 192.168.254.10:60001 -m -w 1
  " | ipvsadm -R""")
  os.system(r'ipvsadm -l')

  # TODO: I don't understand why the new connection still connects to the backup
  # temporary solution: kill the backup
  for proc in psutil.process_iter(['pid', 'name']):
    if proc.info['name'] == 'memcached_reference_model' or proc.info['name'] == 'memcached.replica':
      proc.kill()
  # echo "
  # -a -t 10.0.233.7:11211 -r 192.168.254.10:60002 -m -w 10
  # -d -t 10.0.233.7:11211 -r 192.168.254.10:60001
  # " | ipvsadm -R""") # if the client doesn't use persistent connections, then there's no need to remove the backup from the pool
  # os.system("""
  # echo "
  # -a -t 10.0.233.7:11211 -r 192.168.254.10:60001 -m -w 0
  # " | ipvsadm -R""")

elif exp_type == 'full':
  os.system("""
  echo "
  -d -t 10.0.233.7:11211 -r 192.168.254.10:60000
  -d -t 10.0.233.7:11211 -r 192.168.254.10:60001
  -a -t 10.0.233.7:11211 -r 192.168.254.10:60002 -m -w 10
  " | ipvsadm -R
  """)
  os.system(r'ipvsadm -l')
  boot_command = ["ip", "net", "e", "testx", "memcached", "-p", "60002", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4"]
  # subprocess.Popen(boot_command, start_new_session=True)
  # boot_command = f"taskset -c 0,1,2,3 ip net e testx memcached -p 60000 -d -u root --enable-shutdown -m {str(CACHE_MEM_SIZE)} -t 4 -d"
  # for i in range(0, 1000):
  process = subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
  time.sleep(5)
  # Check if the process has ended
  if process.poll() is not None:
      print(f"The process ended with return code {process.returncode}")
  else:
      print("The process is still running")
  # taskset -c 0,1,2,3 ip net e testx memcached -p 60000 -d -u root --enable-shutdown -m 1024 -t 4
else:
  print('Invalid experiment type')
  exit(1)