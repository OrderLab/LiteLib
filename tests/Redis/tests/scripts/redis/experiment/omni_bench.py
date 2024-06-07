import argparse
import csv
import random
import string
import threading
import time
import queue
from redis import Redis, Sentinel, StrictRedis
from redis.exceptions import ConnectionError, TimeoutError


def random_string(length):
    return ''.join(random.choice(string.ascii_letters + string.digits) for _ in range(length))


def generate_command_args(command, max_key_length, max_value_length):
    if command == 'SET':
        return [random_string(max_key_length), random_string(max_value_length)]
    elif command == 'GET':
        return [random_string(max_key_length)]
    elif command == 'HSET':
        return [random_string(max_key_length), random_string(max_key_length), random_string(max_value_length)]
    elif command == 'HGET':
        return [random_string(max_key_length), random_string(max_key_length)]
    elif command == 'SADD':
        return [random_string(max_key_length), random_string(max_value_length)]
    elif command == 'SPOP':
        return [random_string(max_key_length)]
    elif command == 'RPUSH' or command == 'LPUSH':
        return [random_string(max_key_length), random_string(max_value_length)]
    elif command == 'RPOP' or command == 'LPOP':
        return [random_string(max_key_length)]
    elif command == 'ZADD':
        return [random_string(max_key_length), random.uniform(0, 1), random_string(max_value_length)]
    elif command == 'ZPOPMIN':
        return [random_string(max_key_length)]
    else:
        raise ValueError(f"Unsupported command: {command}")


def execute_command(redis_client, command, args):
    try:
        start_time = time.time()
        response = getattr(redis_client, command.lower())(*args)
        latency = (time.time() - start_time) * 1000  # Calculate latency in milliseconds
        return 'success', response, latency
    except (ConnectionError, TimeoutError):
        return 'missing', None, 1000
    except Exception:
        return 'error', None, 1000


def worker(redis_client, commands, args, result_queue):
    success_count = 0
    missing_count = 0
    error_count = 0

    for command in commands:
        for _ in range(args.num_requests):
            cmd_args = generate_command_args(command, args.max_key_length, args.max_value_length)
            status, _, latency = execute_command(redis_client, command, cmd_args)
            if status == 'success':
                result_queue.put((1, 0, 0, latency))
            elif status == 'missing':
                result_queue.put((0, 1, 0, latency))
            elif status == 'error':
                result_queue.put((0, 0, 1, latency))
            


def get_redis_client(args):
    if args.mode == 'replica':
        sentinel = Sentinel([(args.host, args.port)], socket_timeout=0.1)
        return sentinel.master_for('full_redis')
    elif args.mode == 'lite':
        return StrictRedis(host=args.host, port=args.port)
    else:
        raise ValueError('Invalid mode specified')


def benchmark(args):
    redis_clients = [get_redis_client(args) for _ in range(args.connections)]
    commands = args.commands

    with open(args.output_file, 'w', newline='') as csvfile:
        fieldnames = ['timestamp', 'success_count', 'missing_count', 'error_count', 'latency']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()

        start_time = time.time()
        result_queue = queue.Queue()

        # Create worker threads
        threads = []
        for i in range(args.num_threads):
            redis_client = redis_clients[i % args.connections]  # Distribute clients across threads
            thread = threading.Thread(target=worker, args=(redis_client, commands, args, result_queue))
            thread.start()
            threads.append(thread)

        # Print latency and throughput every second
        while time.time() - start_time < args.duration:
            time.sleep(1)
            elapsed_time = time.time() - start_time
            success_count = missing_count = error_count = 0
            total_latency = 0
            while not result_queue.empty():
                s, m, e, lat = result_queue.get()
                success_count += s
                missing_count += m
                error_count += e
                total_latency += lat
            if success_count + missing_count + error_count == 0:
                exit(0)
            avg_latency = total_latency / (success_count + missing_count + error_count)
            writer.writerow({
                'timestamp': int(elapsed_time),
                'success_count': success_count,
                'missing_count': missing_count,
                'error_count': error_count,
                'latency': avg_latency,
            })
            csvfile.flush()
            print(f'Time: {int(elapsed_time)}s - Success: {success_count}, Missing: {missing_count}, Error: {error_count}, Latency: {avg_latency:.2f}ms')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Redis Benchmark Application')
    parser.add_argument('-c', '--connections', type=int, default=1, help='Number of connections (clients)')
    parser.add_argument('-n', '--num_requests', type=int, required=True, help='Number of requests')
    parser.add_argument('-t', '--num_threads', type=int, default=1, help='Number of threads for parallel execution')
    parser.add_argument('--commands', nargs='+', required=True, help='List of commands to test')
    parser.add_argument('--max_key_length', type=int, default=10, help='Max length of randomized keys')
    parser.add_argument('--max_value_length', type=int, default=10, help='Max length of randomized values')
    parser.add_argument('-l', '--loop', action='store_true', help='Loop requests forever')
    parser.add_argument('-f', '--output_file', default='bench_result.csv', help='Output file name')
    parser.add_argument('--host', default='127.0.0.1', help='Host server IP')
    parser.add_argument('-p', '--port', type=int, default=6379, help='Host server port')
    parser.add_argument('--mode', choices=['replica', 'lite'], default='replica', help='Benchmark mode (replica, lite)')
    parser.add_argument('--duration', type=int, default=1e3, help='Benchmark duration in seconds')

    args = parser.parse_args()
    benchmark(args)
