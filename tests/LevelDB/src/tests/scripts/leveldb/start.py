import argparse
import signal
import time
import os
import utils
import redis
import psutil
import random
import threading


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
    choices=["Full", "Checkpoint", "Lite", "Ebpf"],
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
    "-r",
    "--root_dir",
    type=str,
    required=True,
    help="The root directory of the repository",
)
parser.add_argument(
    "-w", "--work_dir", type=str, required=True, help="The working directory"
)
parser.add_argument(
    "-i", "--checkpoint_interval", type=int, help="The interval of checkpointing"
)
parser.add_argument(
    "-u", "--cpu_limit", type=int, required=True, help="The CPU limit of the whole system"
)
args = parser.parse_args()

os.system(r'cgset -r cpuset.cpus="0-' + str(args.cpu_limit-1) + '" cpulimited')
os.system(r'cgset -r cpu.max="' + str(args.cpu_limit) + '00000 100000" cpulimited')
os.system(r'cgget -g cpu:cpulimited')
os.system(r'cgget -g cpuset:cpulimited')

monitor_log_file = args.work_dir + "/monitor." + args.file_prefix + ".jsonl"
boot_command = [
    "python3",
    args.root_dir + "/tests/LevelDB/src/tests/scripts/leveldb/monitor.py",
    str(args.total_time),
    monitor_log_file,
    str(args.start_time),
]
utils.StartBackgroundProcess(
    boot_command, args.work_dir + "/" + args.file_prefix + "-monitor-log.txt"
)

boot_command = [
    "perf",
    "record",
    "-F",
    "99",
    "-a",
    "-g",
    "-C",
    "0-"+str(args.cpu_limit-1),
    "-o",
    args.work_dir + "/perf.data",
    "--",
    "sleep",
    str(args.total_time - 10),
]
utils.StartBackgroundProcess(
    boot_command, args.work_dir + "/" + args.file_prefix + "-perf-output.log"
)

redis_leveldb_pid = get_pid_by_name("redis-leveldb")
checkpoint_lock = threading.Lock()


def checkpoint_dump(cnt):
    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
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
        args.work_dir + "/" + args.file_prefix + "-dump" + str(cnt) + ".log",
        "--action-script",
        args.root_dir + "/tests/LevelDB/src/tests/scripts/leveldb/criuhelper.sh",
    ]
    with checkpoint_lock:
        process = utils.StartBackgroundProcess(
            boot_command,
            args.work_dir + "/" + args.file_prefix + "-dump-log" + str(cnt) + ".txt",
        )
        process.wait()


def periodic_checkpoint(interval, end_time):
    first_checkpoint_time = time.time()
    i = 1
    while time.time() < end_time:
        checkpoint_dump(i)
        i += 1
        time.sleep(interval - (time.time() - first_checkpoint_time) % interval)
    os.system(f"cp {args.work_dir}/checkpoint-data/foo_before_restore/foo/{args.file_prefix}.log {args.work_dir}")
    os.system(f"cat {args.work_dir}/checkpoint-data/foo_before_restore/foo/reboot_time.log >> {args.work_dir}/{args.file_prefix}.log")


start_time = args.start_time / 1e9
crash_time = start_time + args.crash_time

print(
    f"Current time: {time.time()}, Start time: {start_time}, Crash time: {crash_time}"
)

sleep_for(start_time - time.time())
# ---------------------------------------------------------------- exp begins
checkpoint_thread = None
if args.experiment_type == "Checkpoint":
    target_checkpoint_time = args.crash_time - args.checkpoint_interval / 2
    checkpoint_start_time = start_time + target_checkpoint_time - target_checkpoint_time // args.checkpoint_interval * args.checkpoint_interval
    print(f"Checkpoint start time: {checkpoint_start_time - start_time}")
    checkpoint_thread = threading.Thread(
        target=periodic_checkpoint,
        args=(args.checkpoint_interval, start_time + args.total_time),
    )
    sleep_for(checkpoint_start_time - time.time())
    checkpoint_thread.start()
elif args.experiment_type == "Ebpf":
    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
        args.root_dir + "/tests/LevelDB/src/lite-version/build/LiteLevelDB",
        "-t",
        "5",
        "-s",
        "536870912",
    ]
    utils.StartBackgroundProcess(
        boot_command,
        args.work_dir + "/" + args.file_prefix + ".log",
        False,
        env={"GLOG_stderrthreshold": "0", "GLOG_logtostderr": "1"},
    )

sleep_for(crash_time - time.time())
# ---------------------------------------------------------------- crashes

os.system(r'pgrep "redis-leveldb" | xargs kill -2')

if args.experiment_type == "Full" or  args.experiment_type == "Ebpf":
    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
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
    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
        "criu",
        "restore",
        "-t",
        str(redis_leveldb_pid),
        "-D",
        args.work_dir + "/checkpoint-data",
        "--tcp-close",
        "-d",
        "-vvvv",
        "-o",
        args.work_dir + "/" + args.file_prefix + "-restore.log",
        "--action-script",
        args.root_dir + "/tests/LevelDB/src/tests/scripts/leveldb/criuhelper.sh",
    ]
    with checkpoint_lock:
        while psutil.pid_exists(redis_leveldb_pid):
            time.sleep(0.1)
        process = utils.StartBackgroundProcess(
            boot_command, args.work_dir + "/" + args.file_prefix + "-restore-log.txt"
        )
        process.wait()
    checkpoint_thread.join()
elif args.experiment_type == "Lite":
    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
        args.root_dir + "/tests/LevelDB/src/lite-version/build/Lite/lite_cli",
        "-t",
        "/tmp/lite_LevelDB",
        "-p",
        "/tmp/redis-leveldb.sock",
        "-m",
        "1",
    ]
    utils.StartBackgroundProcess(
        boot_command, args.work_dir + "/" + args.file_prefix + "-lite-cli-log-1.txt"
    )

    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
        args.root_dir + "/tests/LevelDB/src/tests/redis-leveldb/redis-leveldb",
        "-D",
        args.work_dir + "/lite-data",
        "-P",
        "8324",
        "-B",
        str(args.write_buffer_size),
    ]
    utils.StartBackgroundProcess(
        boot_command, args.work_dir + "/" + args.file_prefix + "-backend-log-2.txt"
    )

    result = False
    while not result:
        r = redis.Redis(host="localhost", port=8324)
        try:
            result = r.ping()
        except redis.exceptions.ConnectionError:
            result = False

    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
        args.root_dir + "/tests/LevelDB/src/lite-version/build/Lite/lite_cli",
        "-t",
        "/tmp/lite_LevelDB",
        "-p",
        "/tmp/redis-leveldb.sock",
        "-m",
        "0",
    ]
    utils.StartBackgroundProcess(
        boot_command, args.work_dir + "/" + args.file_prefix + "-lite-cli-log-2.txt"
    )
else:
    raise ValueError("Invalid experiment type")
