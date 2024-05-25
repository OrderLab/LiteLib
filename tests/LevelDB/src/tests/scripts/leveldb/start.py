import argparse
import time
import os
import utils
import redis


def sleep_for(seconds):
    if seconds > 0:
        time.sleep(seconds)


parser = argparse.ArgumentParser(description="Run experiment")
parser.add_argument(
    "-c", "--crash_time", type=int, required=True, help="The crash time"
)
parser.add_argument(
    "-s", "--start_time", type=int, required=True, help="The start time"
)
parser.add_argument(
    "-t",
    "--experiment_type",
    choices=["Full", "Lite"],
    required=True,
    help="The type of the experiment",
)
parser.add_argument(
    "-l", "--total_time", type=int, required=True, help="The total time"
)
parser.add_argument(
    "-f", "--file_prefix", type=str, required=True, help="The file prefix"
)
parser.add_argument(
    "-b",
    "--write_buffer_size",
    type=int,
    help="The size of the write buffer of LevelDB",
)
args = parser.parse_args()

monitor_log_file = f"/workspace/client/monitor." + args.file_prefix + ".jsonl"
boot_command = [
    "python3",
    "/workspace/scripts/leveldb/monitor.py",
    str(args.total_time),
    monitor_log_file,
    str(args.start_time),
]
utils.StartBackgroundProcess(boot_command, "/workspace/scripts/leveldb/monitor-log.txt")

start_time = args.start_time / 1e9
crash_time = start_time + args.crash_time

print(
    f"Current time: {time.time()}, Start time: {start_time}, Crash time: {crash_time}"
)

sleep_for(start_time - time.time())
# ---------------------------------------------------------------- exp begins


sleep_for(crash_time - time.time())
# ---------------------------------------------------------------- crashes

os.system(r'pgrep "redis-leveldb" | xargs kill -2')
os.system(r'pgrep "redis-server" | xargs kill -2')

if args.experiment_type == "Full":
    # time.sleep(10)

    boot_command = [
        "/workspace/redis-leveldb/redis-leveldb",
        "-P",
        "6379",
        "-B",
        str(args.write_buffer_size),
    ]
    utils.StartBackgroundProcess(
        boot_command, "/workspace/client/" + args.file_prefix + ".log", True
    )
else:
    boot_command = [
        "/workspace/server/lite_cli",
        "-t",
        "/tmp/lite_LevelDB",
        "-p",
        "60001",
        "-m",
        "1",
    ]
    utils.StartBackgroundProcess(boot_command, "/workspace/server/lite-cli-log-1.txt")

    # time.sleep(1)

    boot_command = [
        "/workspace/redis-leveldb/redis-leveldb",
        "-P",
        "60001",
        "-B",
        str(args.write_buffer_size),
    ]
    # boot_command = ["redis-server", "--port", "60001"]
    utils.StartBackgroundProcess(
        boot_command, "/workspace/redis-leveldb/backend-log-2.txt"
    )

    # time.sleep(9)

    r = redis.Redis(host="localhost", port=60001)
    result = False
    while not result:
        try:
            result = r.ping()
        except redis.exceptions.ConnectionError:
            result = False

    boot_command = [
        "/workspace/server/lite_cli",
        "-t",
        "/tmp/lite_LevelDB",
        "-p",
        "60001",
        "-m",
        "0",
    ]
    utils.StartBackgroundProcess(boot_command, "/workspace/server/lite-cli-log-2.txt")
