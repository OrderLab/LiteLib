import psutil
import csv
import time
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
            replica = get_redis_replica(16379)
            cpu_usage = replica.cpu_percent(interval=0.5)
            mem_usage = replica.memory_info().rss
            writer.writerow([timestamp, cpu_usage, mem_usage])
            print(f"{timestamp} - CPU: {cpu_usage}%, MEM: {mem_usage} bytes")
            time.sleep(0.5)

if __name__ == '__main__':
    get_cpu_mem_usage("litesys/redis/replica_cpu_mem.csv", 60)