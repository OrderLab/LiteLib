import redis
import time
import argparse
from concurrent.futures import ThreadPoolExecutor

def redis_benchmark(client, command_set, duration):
    def execute_command(command):
        command_name, *args = command
        try:
            start = time.time()
            client.execute_command(command_name, *args)
            latency = time.time() - start
            return latency
        except (redis.exceptions.ConnectionError, redis.exceptions.TimeoutError):
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

def benchmark_client(host, port, command_set, duration):
    while True:
        client = redis.StrictRedis(host=host, port=port, decode_responses=True)
        try:
            avg_latency, throughput = redis_benchmark(client, command_set, duration)
            print(f"Client Avg Latency: {avg_latency:.6f} seconds")
            print(f"Client Throughput: {throughput:.2f} requests per second")
            break
        except redis.exceptions.ConnectionError:
            print("Master connection lost, retrying...")
        except redis.exceptions.TimeoutError:
            print("Master connection timed out, retrying...")
        time.sleep(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Redis benchmark tool")
    parser.add_argument('--host', required=True, help="Redis lite host")
    parser.add_argument('--port', type=int, required=True, help="Redis lite port")
    parser.add_argument('--duration', type=int, default=10, help="Benchmark duration in seconds")
    parser.add_argument('--commands', nargs='+', required=True, help="List of Redis commands to benchmark, e.g., 'PING SET key value'")
    parser.add_argument('--clients', type=int, default=1, help="Number of client connections")

    args = parser.parse_args()

    command_set = [cmd.split() for cmd in args.commands]
    host = args.host
    port = args.port
    duration = args.duration
    num_clients = args.clients

    with ThreadPoolExecutor(max_workers=num_clients) as executor:
        for _ in range(num_clients):
            executor.submit(benchmark_client, host, port, command_set, duration)