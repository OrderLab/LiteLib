import psutil
import csv
import time
import argparse
from datetime import datetime

vanilla_name = "redis-server *:16379"

def get_process_by_name(name):
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        try:
            # Check if the process name contains the given name and port
            if any(name in cmd for cmd in proc.info['cmdline']):
                return proc
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            pass
    return None

def get_cpu_mem_usage(csv_file, duration):
    with open(csv_file, mode='w') as file:
        writer = csv.writer(file)
        writer.writerow(["timestamp", "redis_cpu", "redis_mem"])
        start_time = time.time()
        while time.time() - start_time < duration:
            timestamp = datetime.now().strftime("%H:%M:%S")
            try:
                redis_process = get_process_by_name(vanilla_name)
                if redis_process:
                    redis_cpu = redis_process.cpu_percent(interval=0.5)
                    redis_mem = redis_process.memory_info().rss
                else:
                    redis_cpu = redis_mem = 0
            except Exception as e:
                # print(e)
                redis_cpu = redis_mem = 0
            writer.writerow([timestamp, redis_cpu, redis_mem])
            print(f"{timestamp},{redis_cpu},{redis_mem}")
            time.sleep(0.5)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Monitor CPU and memory usage of Vanilla.')
    parser.add_argument('-t', '--time', type=int, default=180, help='Duration to monitor in seconds (default: 180)')
    args = parser.parse_args()

    get_cpu_mem_usage("data/vanilla_cpu_mem.csv", args.time)