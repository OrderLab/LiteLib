# from pymemcache.client import base
import sys
import os
import time
import subprocess
import psutil
from sync import sync
import socket
import bmemcached
import threading

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


def sleep_for(seconds):
    if seconds > 0:
        time.sleep(seconds)


def get_pid_by_name(name):
    for proc in psutil.process_iter(["pid", "name"]):
        if proc.info["name"] == name:
            return proc.info["pid"]
    return None


memcached_pid = None
checkpoint_lock = None


def checkpoint_dump(cnt):
    boot_command = [
        "sudo",
        "criu",
        "dump",
        "-t",
        str(memcached_pid),
        "-D",
        "/tmp/checkpoint-data",
        "--tcp-close",
        "--ext-unix-sk",
        "--file-locks",
        "--leave-running",
        "--skip-in-flight",
        "-vvvv",
        # "-o",
        # args.work_dir + "/" + args.file_prefix + "-dump" + str(cnt) + ".log",
        # "--action-script",
        # args.root_dir + "/tests/LevelDB/src/tests/scripts/leveldb/criuhelper.sh",
    ]
    with checkpoint_lock:
        process = StartBackgroundProcess(
            boot_command,
            "/tmp/checkpoint-dump-log" + str(cnt) + ".txt",
        )
        process.wait()


def periodic_checkpoint(interval, end_time):
    first_checkpoint_time = time.time()
    i = 1
    while time.time() < end_time:
        checkpoint_dump(i)
        i += 1
        time.sleep(interval - (time.monotonic() - first_checkpoint_time) % interval)

crash_time = int(sys.argv[1:][0])
duration = int(sys.argv[1:][1])
exp_type = sys.argv[1:][2]
CACHE_MEM_SIZE = 4096000
CHECKPOINT_INTERVAL = 10

memcached_pid = get_pid_by_name("memcached")
checkpoint_lock = threading.Lock()
checkpoint_thread = None

if exp_type == 'checkpoint':
  os.system(r'sudo rm -rf /tmp/checkpoint-data')
  os.system(r'sudo mkdir -p /tmp/checkpoint-data')
  target_checkpoint_time = crash_time - CHECKPOINT_INTERVAL / 2
  checkpoint_start_time = target_checkpoint_time - target_checkpoint_time // CHECKPOINT_INTERVAL * CHECKPOINT_INTERVAL
  print(f"Checkpoint start time: {checkpoint_start_time}")
  checkpoint_thread = threading.Thread(
      target=periodic_checkpoint,
      args=(CHECKPOINT_INTERVAL, duration + time.time()),
  )
  sleep_for(checkpoint_start_time - time.time())
  checkpoint_thread.start()

time.sleep(crash_time)

for proc in psutil.process_iter(['pid', 'name']):
  if proc.info['name'] == 'memcached':
    proc.kill()
if exp_type == 'lite':
  path = os.path.expanduser('~/lite_cli')
  boot_command = [path, "-t", "/tmp/lite_memcached", "-p", "/tmp/memcached.sock", "-m", "1"]
  StartBackgroundProcess(boot_command, "/tmp/lite_cli-1.log")
print('failure triggered')

if exp_type == 'lite':
  os.system("rm -f /tmp/memcached.sock")
  boot_command = ["memcached", "-s", "/tmp/memcached.sock", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "32"]
  # boot_command = ["memcached", "-p", "60001", "-d", "-u", "root", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "32"]
  StartBackgroundProcess(boot_command, "/tmp/memcached.log", True)

  # Wait until unix socket is available
  while True:
      try:
          sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
          sock.connect("/tmp/memcached.sock")
          # sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
          # sock.connect(('0.0.0.0', 60001))
          sock.close()
          break
      except (socket.error, FileNotFoundError):
          continue

  path = os.path.expanduser('~/lite_cli')
  boot_command = [path, "-t", "/tmp/lite_memcached", "-p", "/tmp/memcached.sock", "-m", "0"]
  # boot_command = [path, "-t", "/tmp/lite_memcached", "-p", "60001", "-m", "0"]
  StartBackgroundProcess(boot_command, "/tmp/lite_cli-2.log")

  time.sleep(4)
  client = bmemcached.Client(['/tmp/memcached.sock'])
  stats = client.stats()
  for server, server_stats in stats.items():
    if 'curr_items' in server_stats:
        print(f"Current number of keys: {server_stats['curr_items']}")
    else:
        print(f"Could not retrieve 'curr_items' from server: {server}")
elif exp_type == 'full':
  # Wait until port 11211 is available
  # while True:
  #     sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  #     try:
  #         sock.bind(('0.0.0.0', 11211))
  #         sock.close()
  #         break
  #     except socket.error:
  #         continue
  boot_command = ["memcached", "-d", "--enable-shutdown", "-m", str(CACHE_MEM_SIZE), "-t", "32", "-l", "0.0.0.0"]
  StartBackgroundProcess(boot_command, "/tmp/memcached.log", True)
elif exp_type == 'checkpoint':
  boot_command = [
      "sudo",
      "criu",
      "restore",
      "-t",
      str(memcached_pid),
      "-D",
      "/tmp/checkpoint-data",
      "--tcp-close",
      "-d",
      "-vvvv",
      # "-o",
      # "/tmp/checkpoint-restore.log",
      # "--action-script",
      # args.root_dir + "/tests/LevelDB/src/tests/scripts/leveldb/criuhelper.sh",
  ]
  with checkpoint_lock:
      while psutil.pid_exists(memcached_pid):
          time.sleep(0.1)
      process = StartBackgroundProcess(
          boot_command, "/tmp/checkpoint-restore.log"
      )
      process.wait()
  checkpoint_thread.join()
else:
  print('Invalid experiment type')
  exit(1)
