import csv
import time
import argparse
import utils

def monitor_replica(
    full_addr: str = "172.16.0.2",
    full_port: int = 6379,
    repl_addr: str = "172.16.0.2",
    repl_port: int = 6380,
    duration: int = 60,
):
    with open('redis_monitoring.csv', 'w', newline='') as csvfile:
        start_time = time.time()
        writer = csv.writer(csvfile)
        writer.writerow(['Timestamp', 'Process', 'Full CPU Usage', 'Full Memory Usage', 'Full Throughput', 'Replica CPU Usage', 'Replica Memory Usage', 'Replica Throughput'])
        while time.time()-start_time <= duration:
            full = utils.get_process("redis-server", full_port)
            repl = utils.get_process("redis-server", repl_port)
            full_cpu, full_memory, full_throughput = utils.get_usage(full), utils.get_throughput(full_addr, full_port)
            repl_cpu, repl_memory, repl_throughput = utils.get_usage(repl), utils.get_throughput(repl_addr, repl_port)

            writer.writerow([time.time(), 'Full', full_cpu, full_memory, full_throughput, 'Replica', repl_cpu, repl_memory, repl_throughput])
            time.sleep(0.5)