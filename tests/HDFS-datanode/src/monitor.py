import psutil
import sys
import time
import json

class Logger(object):
    def __init__(self, path):
        self.terminal = sys.stdout
        self.log = open(path, "w")

    def write(self, message):
        self.log.write(message)

    def flush(self):
        self.log.flush()

processes = []
process_names = []

def get_process():
    global processes
    global process_names
    for i in range(len(processes)):
        if not processes[i].is_running():
            print(f'process not found: {process_names[i]}')
    processes = []
    process_names = []
    for proc in psutil.process_iter(['name', 'cmdline']):
        try:
            name = proc.info['name']
            cmdline = ' '.join(proc.info['cmdline']) if proc.info['cmdline'] else ''
            if (
                'litehdfsdatanode' in name.lower() or
                'datanode' in name.lower() or
                'datanode' in cmdline.lower()
            ):
                processes.append(proc)
                process_names.append(proc.name())
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue

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
    time_interval = 1
    total_time = int(sys.argv[1])
    log_file = sys.argv[2]

    print(f'total time: {total_time}')
    print(f'log file: {log_file}')
    sys.stdout = Logger(log_file)

    print(f'total time: {total_time}')
    print(f'log file: {log_file}')
    print('logs avg cpu percent between t-1 and t seconds, and mem percent in t second')

    i = 0
    while i <= total_time:
        do(i)
        i += time_interval
        time.sleep(time_interval)
