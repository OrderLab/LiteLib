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

os.system(r'pgrep "cached" | xargs kill -9')
time.sleep(1)
os.system(r'ipvsadm -C')

if exp_type == 'new_lite':
  boot_command = ["memcached", "-p", "60000", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
  # taskset -c 0,1,2,3 memcached -p 60000 -d -u root --enable-shutdown -m 10240 -t 4

  boot_command = ["/workspace/Memcached_codes/LiteMemcached", '-t', '4', '-s', f"{LITE_SIZE}"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
  # taskset -c 0,1,2,3 /workspace/Memcached_codes/LiteMemcached -t 4 -s 10240
else:
  os.system("""
  echo "
  -A -t 10.0.233.7:11211 -s rr
  -a -t 10.0.233.7:11211 -r 192.168.254.10:60000 -m -w 10
  -a -t 10.0.233.7:11211 -r 192.168.254.10:60001 -m -w 0
  " | ipvsadm -R""")
  os.system(r'ipvsadm -l')

  # ---------------------------------------------------------------------------------------------------------------------- temp inner proxy
  os.system("""
  echo "
  -A -t 10.0.233.7:59999 -s rr
  -a -t 10.0.233.7:59999 -r 192.168.254.10:60001 -m -w 1
  " | ipvsadm -R""")
  # ---------------------------------------------------------------------------------------------------------------------- temp inner proxy

  boot_command = ["ip", "net", "e", "testx", "memcached", "-p", "60000", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "4"]
  subprocess.Popen(boot_command, start_new_session=True)
  print(boot_command)
  # taskset -c 0,1,2,3 ip net e testx memcached -p 60000 -d -u root --enable-shutdown -m 1024 -t 4

  if exp_type == 'lite':
    boot_command = ["ip", "net", "e", "testx", '/workspace/Memcached_codes/memcached_reference_model', '-t', '1', '-s', f"{LITE_SIZE}", '-p', "60001"]
    subprocess.Popen(boot_command, start_new_session=True)
    print(boot_command)
    # taskset -c 0,1,2,3 ip net e testx /workspace/Memcached_codes/memcached_reference_model -t 4 -s 1024 -p 60001
  elif exp_type == 'replica':
    boot_command = ["ip", "net", "e", "testx", "memcached.replica", "-p", "60001", "-d", "-u", "root", "--enable-shutdown", "-m", str(REPLICA_SIZE), "-t", "4"]
    subprocess.Popen(boot_command, start_new_session=True)
    print(boot_command)
  elif exp_type != 'full':
    print('Invalid experiment type')
    exit(1)
