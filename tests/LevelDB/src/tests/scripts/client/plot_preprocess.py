import json
import argparse
import math
import numpy as np
import os
import re
from datetime import datetime
from concurrent.futures import ProcessPoolExecutor


def mean2d(arr):
    ret = np.empty(len(arr))
    for i in range(len(arr)):
        if len(arr[i]) == 0:
            ret[i] = np.nan
        else:
            ret[i] = np.mean(arr[i])
    return ret


def p2d(arr, p):
    ret = np.empty(len(arr))
    for i in range(len(arr)):
        if len(arr[i]) == 0:
            ret[i] = np.nan
        else:
            ret[i] = np.percentile(arr[i], p)
    return ret


def convert_ndarray_to_list(data):
    if isinstance(data, np.ndarray):
        return data.tolist()
    elif isinstance(data, list):
        return [convert_ndarray_to_list(item) for item in data]
    elif isinstance(data, dict):
        return {key: convert_ndarray_to_list(value) for key, value in data.items()}
    else:
        return data


parser = argparse.ArgumentParser(description="Process JSON files.")

parser.add_argument("-f", "--filenames", nargs="+", help="The path to the JSON file(s)")
parser.add_argument(
    "-j", "--concurrency", type=int, default=3, help="number of concurrent processes"
)

args = parser.parse_args()

cnt = len(args.filenames)
for filename in args.filenames:
    if not filename.endswith(".jsonl"):
        raise argparse.ArgumentTypeError(
            f"Invalid file type: {filename}. Expected a '.jsonl' file."
        )


def process_file(filename):
    log = []
    begin_time = np.inf
    last_response_time = 0

    def get_index(time):
        return math.floor(time - begin_time)

    def get_timestamp_in_the_end_of_a_line(line):
        match = re.search(r"\b(\d+)\b$", line)
        if match:
            number = match.group(1)
            return float(number) / 1e9 - begin_time
            # return np.floor(float(number) / 1e9 - begin_time)
        return np.nan

    print(f"[{filename}] Starting processing...")
    print(f"[{filename}] Loading and parsing JSON data...")
    with open(filename, "r") as f:
        log = json.load(f)
        for line in log:
            line["begin"] = line["begin"]["secs"] + line["begin"]["nanos"] / 1e9
            begin_time = min(begin_time, line["begin"])
            for query in line["queries"]:
                query["request"] = (
                    query["request"]["secs"] + query["request"]["nanos"] / 1e9
                )
                query["response"] = (
                    query["response"]["secs"] + query["response"]["nanos"] / 1e9
                )
                last_response_time = max(last_response_time, query["response"])

    print(f"[{filename}] Calculating time ranges...")
    total_time = get_index(last_response_time) + 1
    stat = {
        "cnt": np.zeros(total_time),
        "server_lat_list": [[] for _ in range(total_time)],
        "agg_lat_list": [[] for _ in range(total_time)],
        "tries": [[] for _ in range(total_time)],
        "ClientSuccess": np.zeros(total_time),
        "ClientMiss": np.zeros(total_time),
        "ClientTimeout": np.zeros(total_time),
        "ClientError": np.zeros(total_time),
        "ClientStale": np.zeros(total_time),
        "ClientTransactionError": np.zeros(total_time),
        "ServerSuccess": np.zeros(total_time),
        "ServerMiss": np.zeros(total_time),
        "ServerError": np.zeros(total_time),
        "ServerStale": np.zeros(total_time),
        "ServerTimeout": np.zeros(total_time),
        "ServerTransactionError": np.zeros(total_time),
        "lock_wait_time": [[] for _ in range(total_time)],
        "begin_time": begin_time,
    }

    print(f"[{filename}] Processing log entries...")
    for line in log:
        begin_index = get_index(line["queries"][0]["request"])
        stat["cnt"][begin_index] += 1
        if line["queries"][-1]["status"] == "Success":
            stat["agg_lat_list"][begin_index].append(
                line["queries"][-1]["response"] - line["queries"][0]["request"],
            )
            assert len(stat["agg_lat_list"][begin_index]) > 0
            stat["tries"][begin_index].append(len(line["queries"]))
        stat["Client" + line["queries"][-1]["status"]][begin_index] += 1
        for query in line["queries"]:
            request_index = get_index(query["request"])
            if query["status"] == "Success":
                stat["server_lat_list"][request_index].append(
                    query["response"] - query["request"],
                )
            stat["Server" + query["status"]][request_index] += 1
        stat["lock_wait_time"][get_index(line["begin"])].append(
            line["queries"][0]["request"] - line["begin"],
        )
        for i in range(len(line["queries"]) - 1):
            stat["lock_wait_time"][get_index(line["queries"][i]["response"])].append(
                line["queries"][i + 1]["request"] - line["queries"][i]["response"],
            )

    print(f"[{filename}] Calculating statistics...")
    stat["avg_agg_lat"] = mean2d(stat["agg_lat_list"]) * 1000
    stat["p95_agg_lat"] = p2d(stat["agg_lat_list"], 95) * 1000
    stat["avg_server_lat"] = mean2d(stat["server_lat_list"]) * 1000
    stat["p95_server_lat"] = p2d(stat["server_lat_list"], 95) * 1000
    stat["avg_tries"] = mean2d(stat["tries"])
    stat["avg_lock_wait_time"] = mean2d(stat["lock_wait_time"]) * 1000

    print(f"[{filename}] Processing log file for timestamps...")
    log_file = filename[:-6] + ".log"
    stat["crash_time"] = np.nan
    stat["reboot_time"] = np.nan
    stat["replay_time"] = np.nan
    if not os.path.exists(log_file):
        print(
            f"[{filename}] Log file {log_file} does not exist, won't plot special timestamps"
        )
    else:
        begin_time = stat["begin_time"]
        with open(log_file, "r") as f:
            lines = f.readlines()
        for line in lines:
            if ("ntering emergency mode" in line or "crash time" in line) and stat[
                "crash_time"
            ] is np.nan:
                stat["crash_time"] = get_timestamp_in_the_end_of_a_line(line)
            if "Exiting emergency mode" in line or "boot time" in line:
                stat["reboot_time"] = get_timestamp_in_the_end_of_a_line(line)
            if "Exited emergency mode" in line:
                stat["replay_time"] = get_timestamp_in_the_end_of_a_line(line)
        begin_time_str = datetime.fromtimestamp(begin_time).strftime(
            "%Y-%m-%d %H:%M:%S"
        )
        print(
            f"[{filename}] Case: {filename[:-6]}, begin_time: {begin_time_str}, crash time: {stat['crash_time']}, reboot time: {stat['reboot_time']}, replay time: {stat['replay_time']}"
        )

    print(f"[{filename}] Processing monitor data...")
    dir_name, file_name = os.path.split(filename)
    monitor_filename = os.path.join(dir_name, "monitor." + file_name)
    if not monitor_filename.endswith(".jsonl"):
        raise argparse.ArgumentTypeError(
            f"Invalid file type: {monitor_filename}. Expected a '.jsonl' file."
        )
    with open(monitor_filename, "r") as file:
        lines = file.readlines()
    process_usages = {}
    for line in lines:
        try:
            data = json.loads(line)
            time = 0
            for process_name, process_info in data.items():
                if process_name == "time":
                    time = int(process_info)
                else:
                    if process_name not in process_usages:
                        process_usages[process_name] = {
                            "cpu": np.full(len(stat["cnt"]) + 100, np.nan),
                            "mem": np.full(len(stat["cnt"]) + 100, np.nan),
                        }
                    process_usages[process_name]["cpu"][time] = process_info["cpu"]
                    process_usages[process_name]["mem"][time] = process_info["mem"]
        except json.JSONDecodeError:
            pass

    print(f"[{filename}] Finalizing resource usage data...")
    for process_usage in process_usages.values():
        cpu = process_usage["cpu"][0 : len(stat["cnt"])]
        mem = process_usage["mem"][0 : len(stat["cnt"])]
        mem = mem / 1024.0 / 1024.0
        process_usage["cpu"] = cpu
        process_usage["mem"] = mem
    ordered_process_usages = {}
    if "redis-leveldb" in process_usages:
        ordered_process_usages["redis-leveldb"] = process_usages["redis-leveldb"]
    elif "redis-leveldb-vanilla" in process_usages:
        ordered_process_usages["redis-leveldb-vanilla"] = process_usages[
            "redis-leveldb-vanilla"
        ]
    for process_name in sorted(process_usages.keys()):
        if process_name != "redis-leveldb" and process_name != "redis-leveldb-vanilla":
            ordered_process_usages[process_name] = process_usages[process_name]
    stat["resource"] = ordered_process_usages

    print(f"[{filename}] Saving statistics to file...")
    stat_file = filename[:-6] + ".stat.json"

    stat = convert_ndarray_to_list(stat)

    with open(stat_file, "w") as f:
        json.dump(stat, f)
    print(f"[{filename}] Finished processing")


with ProcessPoolExecutor(max_workers=args.concurrency) as executor:
    logs = list(executor.map(process_file, args.filenames[:cnt]))
