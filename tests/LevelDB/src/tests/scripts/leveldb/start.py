import argparse
import time
import os
import utils
import redis
import psutil


def sleep_for(seconds):
    if seconds > 0:
        time.sleep(seconds)


def get_pid_by_name(name):
    for proc in psutil.process_iter(["pid", "name"]):
        if proc.info["name"] == name:
            return proc.info["pid"]
    return None


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
    choices=["Full", "Checkpoint", "Lite"],
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
utils.StartBackgroundProcess(boot_command, "/workspace/scripts/leveldb/" + args.file_prefix + "-monitor-log.txt")

start_time = args.start_time / 1e9
crash_time = start_time + args.crash_time

print(
    f"Current time: {time.time()}, Start time: {start_time}, Crash time: {crash_time}"
)

sleep_for(start_time - time.time())
# ---------------------------------------------------------------- exp begins

redis_leveldb_pid = get_pid_by_name("redis-leveldb")
if args.experiment_type == "Checkpoint":
    boot_command = [
        "criu",
        "dump",
        "-t",
        str(redis_leveldb_pid),
        "-D",
        "/workspace/scripts/leveldb/criu",
        "--tcp-close",
        "--ext-unix-sk",
        "--file-locks",
        "--leave-running",
        "--skip-in-flight",
        "-vvvv",
        "-o",
        "/workspace/scripts/leveldb/" + args.file_prefix + "-dump.log",
		"--action-script",
        "/workspace/scripts/leveldb/criuhelper.sh"
    ]
    utils.StartBackgroundProcess(
        boot_command, "/workspace/scripts/leveldb/" + args.file_prefix + "-dump-log.txt"
    )

sleep_for(crash_time - time.time())
# ---------------------------------------------------------------- crashes

os.system(r'pgrep "redis-leveldb" | xargs kill -2')
os.system(r'pgrep "redis-server" | xargs kill -2')

if args.experiment_type == "Full":
    # time.sleep(10)

    boot_command = [
        "taskset",
        "-c",
        "0,1",
        "/workspace/redis-leveldb/redis-leveldb",
        "-P",
        "6379",
        "-B",
        str(args.write_buffer_size),
    ]
    utils.StartBackgroundProcess(
        boot_command, "/workspace/client/" + args.file_prefix + ".log", True
    )
elif args.experiment_type == "Checkpoint":
    sleep_for(1)
    boot_command = [
        "criu",
        "restore",
        "-t",
        str(redis_leveldb_pid),
        "-D",
        "/workspace/scripts/leveldb/criu",
        "--tcp-close",
        "--restore-detached",
        "-vvvv",
        "-o",
        "/workspace/scripts/leveldb/" + args.file_prefix + "-restore.log",
	    "--action-script",
        "/workspace/scripts/leveldb/criuhelper.sh",
    ]
    utils.StartBackgroundProcess(
        boot_command, "/workspace/scripts/leveldb/" + args.file_prefix + "-restore-log.txt"
    )
else:
    boot_command = [
        "/workspace/server/lite_cli",
        "-t",
        "/tmp/lite_LevelDB",
        "-p",
        "/tmp/redis-leveldb.sock",
        "-m",
        "1",
    ]
    utils.StartBackgroundProcess(boot_command, "/workspace/server/" + args.file_prefix + "-lite-cli-log-1.txt")

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
        boot_command, "/workspace/redis-leveldb/" + args.file_prefix + "-backend-log-2.txt"
    )

    # time.sleep(9)

    result = False
    while not result:
        r = redis.Redis(host="localhost", port=60001)
        try:
            result = r.ping()
        except redis.exceptions.ConnectionError:
            result = False

    boot_command = [
        "/workspace/server/lite_cli",
        "-t",
        "/tmp/lite_LevelDB",
        "-p",
        "/tmp/redis-leveldb.sock",
        "-m",
        "0",
    ]
    utils.StartBackgroundProcess(boot_command, "/workspace/server/" + args.file_prefix + "-lite-cli-log-2.txt")
