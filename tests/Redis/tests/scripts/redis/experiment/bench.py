import redis
import time
import argparse
import threading
from concurrent.futures import ThreadPoolExecutor


def get_redis_master(sentinel_host, sentinel_port):
    sentinel = redis.StrictRedis(host=sentinel_host, port=sentinel_port, decode_responses=True)
    while True:
        try:
            masters = sentinel.sentinel_masters()
            for master_name, master_info in masters.items():
                if 'master' in master_info['flags']:
                    return master_info['ip'], master_info['port']
        except redis.exceptions.ConnectionError as e:
            print(f"Failed to connect to Sentinel at {sentinel_host} : {sentinel_port}, retrying... Error: {e}")
            time.sleep(1)


def redis_benchmark(client, command_set, duration):

    def execute_command(command):
        command_name, *args = command
        try:
            start = time.time()
            client.execute_command(command_name, *args)
            latency = time.time() - start
            return latency
        except redis.exceptions.ConnectionError:
            return None
        except redis.exceptions.TimeoutError:
            return None

    start_time = time.time()
    end_time = start_time + duration
    latencies = []
    rps = 0

    while time.time() < end_time:
        for command in command_set:
            latency = execute_command(command)
            if latency is not None:
                latencies.append(latency)
                rps += 1
            else:
                raise redis.exceptions.ConnectionError
            if time.time() >= end_time:
                break

    avg_latency = sum(latencies) / len(latencies) if latencies else 0
    throughput = rps / duration

    return avg_latency, throughput


def benchmark_client(sentinel_host, sentinel_port, command_set, duration):
    while True:
        master_host, master_port = get_redis_master(sentinel_host, sentinel_port)
        print(f"Connected to Redis master at {master_host}:{master_port}")
        client = redis.StrictRedis(host=master_host, port=master_port, decode_responses=True)
        while True:
            try:
                avg_latency, throughput = redis_benchmark(client, command_set, duration)
                print(f"Client Avg Latency: {avg_latency:.6f} seconds")
                print(f"Client Throughput: {throughput:.2f} requests per second")
            except redis.exceptions.ConnectionError:
                print("Master connection lost, querying Sentinel for new master...")
                break
            except redis.exceptions.TimeoutError:
                print("Master connection timed out, waiting for Sentinel to find new master...")
                break
        time.sleep(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Redis benchmark tool using Sentinel")
    parser.add_argument('--sentinel-host', required=True, help="Sentinel host")
    parser.add_argument('--sentinel-port', type=int, required=True, help="Sentinel port")
    parser.add_argument('--duration', type=int, default=10, help="Benchmark duration in seconds")
    parser.add_argument('--commands', nargs='+', required=True, help="List of Redis commands to benchmark, e.g., 'PING SET key value'")
    parser.add_argument('--clients', type=int, default=1, help="Number of client connections")

    args = parser.parse_args()

    command_set = [cmd.split() for cmd in args.commands]
    sentinel_host = args.sentinel_host
    sentinel_port = args.sentinel_port
    duration = args.duration
    num_clients = args.clients

    with ThreadPoolExecutor(max_workers=num_clients) as executor:
        for _ in range(num_clients):
            executor.submit(benchmark_client, sentinel_host, sentinel_port, command_set, duration)
