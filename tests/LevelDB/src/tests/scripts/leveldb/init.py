import os
import argparse
import utils
import time

parser = argparse.ArgumentParser(description="Init experiment")
parser.add_argument(
    "-t",
    "--experiment_type",
    choices=["Full", "Lite"],
    required=True,
    help="The type of the experiment",
)
parser.add_argument(
    "-n", "--num_threads", type=int, help="The number of threads of the lite version"
)
parser.add_argument(
    "-s", "--memory_size", type=str, help="The memory limit of the lite version"
)
parser.add_argument(
    "-b",
    "--write_buffer_size",
    type=int,
    help="The size of the write buffer of LevelDB",
)
parser.add_argument(
    "-f", "--file_prefix", type=str, required=True, help="The file prefix"
)
args = parser.parse_args()

os.system(r'pgrep "redis-leveldb" | xargs kill -9')
os.system(r"rm redis.db -r")
os.system(r'pgrep "LiteLevelDB" | xargs kill -9')
os.system(r'pgrep "lite_cli" | xargs kill -9')
os.system(r'pgrep "redis-server" | xargs kill -9')
os.system(r"rm dump.rdb")
time.sleep(1)

if args.experiment_type == "Full":
    boot_command = [
        "/workspace/redis-leveldb/redis-leveldb",
        "-P",
        "6379",
        "-B",
        str(args.write_buffer_size),
    ]
    utils.StartBackgroundProcess(
        boot_command, "/workspace/client/" + args.file_prefix + ".log"
    )
else:
    # boot_command = ["redis-server", "--port", "60000"]
    boot_command = [
        "/workspace/redis-leveldb/redis-leveldb",
        "-P",
        "60000",
        "-B",
        str(args.write_buffer_size),
    ]
    utils.StartBackgroundProcess(
        boot_command, "/workspace/redis-leveldb/backend-log-1.txt"
    )

    boot_command = [
        "/workspace/server/LiteLevelDB",
        "-t",
        str(args.num_threads),
        "-s",
        args.memory_size,
    ]
    utils.StartBackgroundProcess(
        boot_command,
        "/workspace/client/" + args.file_prefix + ".log",
        False,
        env={"GLOG_stderrthreshold": "0", "GLOG_logtostderr": "1"},
    )
