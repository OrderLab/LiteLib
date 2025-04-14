import psutil
import sys
import time
import json
import os

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

processes = []
process_names = []

def get_process():
  global processes
  global process_names
  for i in range(0, len(processes)):
    if processes[i].is_running() == False:
      print(f'process not found: {process_names[i]}')
  processes = []
  process_names = []
  for proc in psutil.process_iter():
    if 'lite' in proc.name() or "Lite" in proc.name() or "memcached" in proc.name():
      processes.append(proc)
      process_names.append(proc.name())

def do(i):
  global processes
  global process_names
  exception = True
  data = {"time": i}
  while exception:
    try:
      get_process()
      for name in process_names:
        data[name] = {"cpu": 0.0, "mem": 0.0}
      for proc in processes:
        cpu = None
        mem = None
        with proc.oneshot():
          cpu = proc.cpu_percent(interval=None)
          mem = proc.memory_info().rss
        data[proc.name()]['cpu'] += cpu
        data[proc.name()]['mem'] += mem
    except Exception as error:
      print("An error occurred:", error)
      exception = True
    else:
      exception = False
  print(json.dumps(data))

if __name__ == '__main__':
  time_interval = 1.0
  total_time = int(sys.argv[1:][0]) + 5
  print(f'total time: {total_time}')
  
  # Get the directory of the current script
  script_dir = os.path.dirname(os.path.abspath(__file__))
  # Create log directory if it doesn't exist
  log_dir = os.path.join(script_dir, 'logs')
  os.makedirs(log_dir, exist_ok=True)
  # Construct the full log file path
  log_file = os.path.join(log_dir, sys.argv[2])
  
  print(f'log file: {log_file}')
  sys.stdout = Logger(log_file)

  start_time = time.time()

  print(f'start time: {start_time}')
  print(f'total time: {total_time}')
  print(f'log file: {sys.argv[2]}')
  print(f'logs avg cpu percent between t-1 and t seconds, and mem percent in t second')
  i = -1
  while i < total_time + 1:
    do(i)
    i = i + 1
    time.sleep(time_interval - ((time.time() - start_time / 1e9) % time_interval))