import psutil
import csv
import time
import argparse
from datetime import datetime

def get_redis_replica(port):
    for proc in psutil.process_iter(['pid', 'name']):
        try:
            connections = proc.net_connections()
            for conn in connections:
                if conn.laddr.port == port:
                    return proc
        except (psutil.AccessDenied, psutil.NoSuchProcess):
            continue
    return None

def get_cpu_mem_usage(csv_file, duration):
    with open(csv_file, mode='w') as file:
        writer = csv.writer(file)
        writer.writerow(["timestamp", "replica_cpu", "replica_mem"])
        start_time = time.time()
        while time.time() - start_time < duration:
            timestamp = datetime.now().strftime("%H:%M:%S")
            try:
                replica = get_redis_replica(16379)
                cpu_usage = replica.cpu_percent(interval=0.5)
                mem_usage = replica.memory_info().rss
                print(f"{timestamp},{cpu_usage},{mem_usage}")
            except Exception as e:
                cpu_usage = 0
                mem_usage = 0
                print(f"{timestamp},0,0")
            writer.writerow([timestamp, cpu_usage, mem_usage])
            time.sleep(0.5)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Monitor CPU and memory usage of processes.')
    parser.add_argument('-t', '--time', type=int, default=180, help='Duration to monitor in seconds (default: 180)')
    args = parser.parse_args()
    
    get_cpu_mem_usage("litesys/redis/replica_cpu_mem.csv", args.time)