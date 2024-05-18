import csv
import time
import utils
import argparse


def monitor_replica(full_addr: str="172.16.0.2", full_port: int=6379, repl_addr: str="172.16.0.2", repl_port: int=6380, duration: int=60):
    with open('redis_monitoring.csv', 'w', newline='') as csvfile:
        start_time = time.time()
        writer = csv.writer(csvfile)
        writer.writerow(['Timestamp', 'Full CPU Usage', 'Full Memory Usage', 'Full Throughput', 'Replica CPU Usage', 'Replica Memory Usage', 'Replica Throughput'])
        
        last_time = time.time()
        last_full_cmd_done = utils.cmd_done_cnt(full_addr, full_port)
        last_repl_cmd_done = utils.cmd_done_cnt(repl_addr, repl_port)
        
        while time.time() - start_time <= duration:
            full = utils.get_process("redis-server", full_port)
            repl = utils.get_process("redis-server", repl_port)
            
            full_cmd_done = utils.cmd_done_cnt(full_addr, full_port)
            repl_cmd_done = utils.cmd_done_cnt(repl_addr, repl_port)
            full_throughput = (full_cmd_done - last_full_cmd_done) / (time.time() - last_time) if full_cmd_done - last_full_cmd_done > 0 else 0
            repl_throughput = (repl_cmd_done - last_repl_cmd_done) / (time.time() - last_time) if repl_cmd_done - last_repl_cmd_done > 0 else 0
            
            last_time = time.time()
            last_full_cmd_done = full_cmd_done
            last_repl_cmd_done = repl_cmd_done
            
            full_cpu, full_memory = utils.get_usage(full)
            repl_cpu, repl_memory = utils.get_usage(repl)
            writer.writerow([time.time()-start_time, full_cpu, full_memory, full_throughput, repl_cpu, repl_memory, repl_throughput])
            time.sleep(0.5)
            
            
# Needs to be fixed, lite does not accept INFO command to show the number of commands done
def monitor_lite(full_addr: str="172.16.0.2", full_port: int=6379, lite_addr: str="172.16.0.2", lite_port: int=6479, duration: int=60):
    with open('redis_monitoring.csv', 'w', newline='') as csvfile:
        start_time = time.time()
        writer = csv.writer(csvfile)
        writer.writerow(['Timestamp', 'Process', 'Full CPU Usage', 'Full Memory Usage', 'Full Throughput', 'Lite CPU Usage', 'Lite Memory Usage', 'Lite Throughput'])
        while time.time() - start_time <= duration:
            full = utils.get_process("redis-server", full_port)
            lite = utils.get_process("lite_cli", lite_port)
            full_cmd_done_start = utils.cmd_done_cnt(full_addr, full_port)
            # lite_cmd_done_start = utils.cmd_done_cnt(lite_addr, lite_port)
            time_start = time.time()
            time.sleep(0.5)
            full_cmd_done_end = utils.cmd_done_cnt(full_addr, full_port)
            # lite_cmd_done_end = utils.cmd_done_cnt(lite_addr, lite_port)
            full_throughput = (full_cmd_done_end - full_cmd_done_start) / (time.time() - time_start)
            # lite_throughput = (lite_cmd_done_end - lite_cmd_done_start) / (time.time() - time_start)
            full_cpu, full_memory = utils.get_usage(full)
            lite_cpu, lite_memory = utils.get_usage(lite)
            writer.writerow([time.time(), 'Full', full_cpu, full_memory, full_throughput, 'Lite', lite_cpu, lite_memory, lite_throughput])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Redis Monitoring")
    parser.add_argument("--mode", choices=["replica", "lite"], help="Monitoring mode")
    parser.add_argument("--full_addr", default="172.16.0.2", help="Full address")
    parser.add_argument("--full_port", type=int, default=6379, help="Full port")
    parser.add_argument("--repl_addr", default="172.16.0.2", help="Replica address")
    parser.add_argument("--repl_port", type=int, default=6380, help="Replica port")
    parser.add_argument("--lite_addr", default="172.16.0.2", help="Lite address")
    parser.add_argument("--lite_port", type=int, default=6479, help="Lite port")
    parser.add_argument("--duration", type=int, default=60, help="Monitoring duration")

    args = parser.parse_args()

    if args.mode == "replica":
        print("Monitoring replica")
        monitor_replica(args.full_addr, args.full_port, args.repl_addr, args.repl_port, args.duration)
    else:
        print("Monitoring lite")
        monitor_lite(args.full_addr, args.full_port, args.lite_addr, args.lite_port, args.duration)
