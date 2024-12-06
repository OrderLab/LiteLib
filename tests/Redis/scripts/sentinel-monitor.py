import psutil
import csv
import time
from datetime import datetime

def get_redis_sentinels(ports):
    sentinels = []
    for proc in psutil.process_iter(['pid', 'name']):
        try:
            connections = proc.net_connections()
            for conn in connections:
                if conn.laddr.port in ports:
                    sentinels.append(proc)
                    break
        except (psutil.AccessDenied, psutil.NoSuchProcess):
            continue
    return sentinels

def get_cpu_mem_usage(csv_file, duration):
    with open(csv_file, mode='w') as file:
        writer = csv.writer(file)
        writer.writerow(["timestamp", "sentinel_26479_cpu", "sentinel_26479_mem", "sentinel_26480_cpu", "sentinel_26480_mem", "sentinel_26481_cpu", "sentinel_26481_mem"])
        start_time = time.time()
        while time.time() - start_time < duration:
            timestamp = datetime.now().strftime("%H:%M:%S")
            sentinels = get_redis_sentinels([26479, 26480, 26481])
            sentinel_data = []
            for sentinel in sentinels:
                sentinel_data.append(sentinel.cpu_percent(interval=0.5))
                sentinel_data.append(sentinel.memory_info().rss)
            
            writer.writerow([timestamp] + sentinel_data)
            print(f"{timestamp}: sentinel_26479_cpu={sentinel_data[0]}, sentinel_26479_mem={sentinel_data[1]}, sentinel_26480_cpu={sentinel_data[2]}, sentinel_26480_mem={sentinel_data[3]}, sentinel_26481_cpu={sentinel_data[4]}, sentinel_26481_mem={sentinel_data[5]}")
            time.sleep(0.5)

if __name__ == '__main__':
    get_cpu_mem_usage("litesys/redis/sentinel_cpu_mem.csv", 60)