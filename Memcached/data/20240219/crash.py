# from pymemcache.client import base
import sys
import os
import time
import subprocess

crash_time = int(sys.argv[1:][0])
exp_type = sys.argv[1:][1]

kill_command = r'pgrep -x "memcached" | xargs kill -9'
# os.system(kill_command)
# subprocess.run(["bash", "-c", f"'{kill_command}'"], shell=True, timeout=10)
subprocess.run(kill_command, shell=True, timeout=10)
print('failure triggered')

time.sleep(crash_time)

if exp_type == 'lite' or exp_type == 'replica':
  os.system("""
  echo "
  -d -t 10.0.233.7:11211 -r 192.168.254.10:60000
  -e -t 10.0.233.7:11211 -r 192.168.254.10:60001 -m -w 10
  " | ipvsadm -R
  """)
  os.system(r'ipvsadm -l')
elif exp_type == 'full':
  CACHE_MEM_SIZE = 10240
  boot_command = ["taskset", "-c", "0,1,2,3", "ip", "net", "e", "testx", "memcached", "-p", "60000", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4"]
  # subprocess.Popen(boot_command, start_new_session=True)
  # boot_command = f"taskset -c 0,1,2,3 ip net e testx memcached -p 60000 -d -u root --enable-shutdown -m {str(CACHE_MEM_SIZE)} -t 4 -d"
  for i in range(0, 100):
    subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
  # taskset -c 0,1,2,3 ip net e testx memcached -p 60000 -d -u root --enable-shutdown -m 1024 -t 4
else:
  print('Invalid experiment type')
  exit(1)