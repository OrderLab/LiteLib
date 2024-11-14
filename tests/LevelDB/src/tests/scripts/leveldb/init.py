import os
import argparse
import utils
import time

parser = argparse.ArgumentParser(description="Init experiment")
parser.add_argument(
    "-t",
    "--experiment_type",
    choices=["Full", "Checkpoint", "Lite"],
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
args = parser.parse_args()

os.system(r"mkdir -p " + args.work_dir)
os.system(r"chmod 777 " + args.work_dir)

os.system(r'pgrep "redis-leveldb" | xargs kill -9')
os.system(r'pgrep "LiteLevelDB" | xargs kill -9')
os.system(r'pgrep "lite_cli" | xargs kill -9')
os.system(r'pgrep "redis-server" | xargs kill -9')
os.system(r"rm dump.rdb")
time.sleep(1)

os.system(r'cgdelete -g cpu:/cpulimited')
os.system(r'cgcreate -g cpu:/cpulimited')
os.system(r'cgset -r cpu.max="4000000 100000" cpulimited') # 40 cores
os.system(r'cgget -g cpu:cpulimited')

if args.experiment_type == "Full":
    os.system(r"rm -rf " + args.work_dir + "/full-data")
    os.system(r"mkdir -p " + args.work_dir + "/full-data")
    # boot_command = ["redis-server", "--port", "6379", "--protected-mode", "no"]
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
        boot_command, args.work_dir + "/" + args.file_prefix + ".log"
    )
elif args.experiment_type == "Checkpoint":
    os.system(r"rm -rf " + args.work_dir + "/checkpoint-data")
    os.system(r"mkdir -p " + args.work_dir + "/checkpoint-data/foo")
    os.system(r"mkdir -p " + args.work_dir + "/checkpoint-data/foobak")
    os.system(r"mkdir -p " + args.work_dir + "/checkpoint-data/foo_before_restore")

    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
        args.root_dir + "/tests/LevelDB/src/tests/redis-leveldb/redis-leveldb",
        "-D",
        args.work_dir + "/checkpoint-data/foo",
        "-P",
        "6379",
        "-B",
        str(args.write_buffer_size),
    ]
    utils.StartBackgroundProcess(
        boot_command,
        args.work_dir + "/checkpoint-data/foo/" + args.file_prefix + ".log",
    )
elif args.experiment_type == "Lite":
    os.system(r"rm -rf " + args.work_dir + "/lite-data")
    os.system(r"mkdir -p " + args.work_dir + "/lite-data")
    # boot_command = ["redis-server", "--port", "60000", "--protected-mode", "no"]
    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
        args.root_dir + "/tests/LevelDB/src/tests/redis-leveldb/redis-leveldb",
        "-D",
        args.work_dir + "/lite-data",
        "-P",
        "60000",
        "-B",
        str(args.write_buffer_size),
    ]
    utils.StartBackgroundProcess(
        boot_command, args.work_dir + "/" + args.file_prefix + "-backend-log-1.txt"
    )

    boot_command = [
        "cgexec",
        "-g",
        "cpu:cpulimited",
        args.root_dir + "/tests/LevelDB/src/lite-version/build/LiteLevelDB",
        "-t",
        str(args.num_threads),
        "-s",
        args.memory_size,
    ]
    utils.StartBackgroundProcess(
        boot_command,
        args.work_dir + "/" + args.file_prefix + ".log",
        False,
        env={"GLOG_stderrthreshold": "0", "GLOG_logtostderr": "1"},
    )
else:
    raise ValueError("Invalid experiment type")
