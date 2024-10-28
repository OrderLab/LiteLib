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
parser.add_argument(
    "-r", "--root_dir", type=str, required=True, help="The root directory of the repository"
)
parser.add_argument(
    "-w", "--work_dir", type=str, required=True, help="The working directory"
)
args = parser.parse_args()

monitor_log_file = args.work_dir + "/monitor." + args.file_prefix + ".jsonl"
boot_command = [
    "python3",
    args.root_dir + "/tests/LevelDB/src/tests/scripts/leveldb/monitor.py",
    str(args.total_time),
    monitor_log_file,
    str(args.start_time),
]
utils.StartBackgroundProcess(boot_command, args.work_dir + "/" + args.file_prefix + "-monitor-log.txt")

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
        args.work_dir + "/checkpoint-data",
        "--tcp-close",
        "--ext-unix-sk",
        "--file-locks",
        "--leave-running",
        "--skip-in-flight",
        "-vvvv",
        "-o",
        args.work_dir + "/" + args.file_prefix + "-dump.log",
		"--action-script",
        args.root_dir + "/tests/LevelDB/src/tests/scripts/leveldb/criuhelper.sh"
    ]
    utils.StartBackgroundProcess(
        boot_command, args.work_dir + "/" + args.file_prefix + "-dump-log.txt"
    )

sleep_for(crash_time - time.time())
# ---------------------------------------------------------------- crashes

os.system(r'pgrep "redis-leveldb" | xargs kill -2')
os.system(r'pgrep "redis-server" | xargs kill -2')

if args.experiment_type == "Full":
    # time.sleep(10)

    boot_command = [
        args.root_dir + "/tests/LevelDB/src/tests/redis-leveldb/redis-leveldb",
        "-D",
        args.work_dir + "/full-data",
        "-P",
        "6379",
        "-B",
        str(args.write_buffer_size),
    ]
    utils.StartBackgroundProcess(
        boot_command, args.work_dir + "/" + args.file_prefix + ".log", True
    )
elif args.experiment_type == "Checkpoint":
    sleep_for(1)
    boot_command = [
        "criu",
        "restore",
        "-t",
        str(redis_leveldb_pid),
        "-D",
        args.work_dir + "/checkpoint-data",
        "--tcp-close",
        "--restore-detached",
        "-vvvv",
        "-o",
        args.work_dir + "/" + args.file_prefix + "-restore.log",
	    "--action-script",
        args.root_dir + "/tests/LevelDB/src/tests/scripts/leveldb/criuhelper.sh",
    ]
    utils.StartBackgroundProcess(
        boot_command, args.work_dir + "/" + args.file_prefix + "-restore-log.txt"
    )
elif args.experiment_type == "Lite":
    boot_command = [
        args.root_dir + "/tests/LevelDB/src/lite-version/build/Lite/lite_cli",
        "-t",
        "/tmp/lite_LevelDB",
        "-p",
        "/tmp/redis-leveldb.sock",
        "-m",
        "1",
    ]
    utils.StartBackgroundProcess(boot_command, args.work_dir + "/" + args.file_prefix + "-lite-cli-log-1.txt")

    # time.sleep(1)

    boot_command = [
        args.root_dir + "/tests/LevelDB/src/tests/redis-leveldb/redis-leveldb",
        "-D",
        args.work_dir + "/lite-data",
        "-P",
        "60001",
        "-B",
        str(args.write_buffer_size),
    ]
    # boot_command = ["redis-server", "--port", "60001"]
    utils.StartBackgroundProcess(
        boot_command, args.work_dir + "/" + args.file_prefix + "-backend-log-2.txt"
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
        args.root_dir + "/tests/LevelDB/src/lite-version/build/Lite/lite_cli",
        "-t",
        "/tmp/lite_LevelDB",
        "-p",
        "/tmp/redis-leveldb.sock",
        "-m",
        "0",
    ]
    utils.StartBackgroundProcess(boot_command, args.work_dir + "/" + args.file_prefix + "-lite-cli-log-2.txt")
else:
    raise ValueError("Invalid experiment type")
