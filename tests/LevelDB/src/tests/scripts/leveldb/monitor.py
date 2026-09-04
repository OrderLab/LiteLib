import psutil
import sys
import time
import json

class Logger(object):
  def __init__(self, path):
    self.terminal = sys.stdout
    self.log = open(path, "w")

  def write(self, message):
    # self.terminal.write(message)
    self.log.write(message)

  def flush(self):
    # self.terminal.flush()
    self.log.flush()

def do(i):
  data = {"time": i}
  for proc in psutil.process_iter(["name"]):
    try:
      name = proc.info["name"] or ""
      if not any(token in name.lower() for token in (
        "lite", "leveldb", "criu", "socket"
      )):
        continue
      with proc.oneshot():
        cpu = proc.cpu_percent(interval=None)
        mem = proc.memory_info().rss
      info = data.setdefault(name, {"cpu": 0.0, "mem": 0.0})
      info["cpu"] += cpu
      info["mem"] += mem
    except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
      continue
  print(json.dumps(data))

if __name__ == '__main__':
  time_interval = 1.0
  total_time = int(sys.argv[1:][0]) + 5
  start_time = int(sys.argv[3:][0])
  print(f'total time: {total_time}')
  print(f'start time: {start_time}')
  print(f'log file: {sys.argv[2]}')
  sys.stdout = Logger(sys.argv[2])
  print(f'total time: {total_time}')
  print(f'start time: {start_time}')
  print(f'log file: {sys.argv[2]}')
  print(f'logs avg cpu percent between t-1 and t seconds, and mem percent in t second')

  i = -1
  time_delta = start_time / 1e9 - time.time()
  time.sleep(time_delta if time_delta > 0 else 0)
  print(f'waited for {time_delta} seconds')
  while i < total_time + 1:
    do(i)
    i = i + 1
    time.sleep(time_interval - ((time.time() - start_time / 1e9) % time_interval))