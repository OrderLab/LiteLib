import json
import os
import argparse
import numpy as np


parser = argparse.ArgumentParser(description="Process JSON files.")
parser.add_argument("-f", "--filenames", nargs="+", help="The path to the JSON file(s)")
parser.add_argument("-s", "--start", default=0, type=int, help="The start time")
parser.add_argument("-e", "--end", default=None, type=int, help="The end time")

args = parser.parse_args()

def process_monitor(filename):
    print(f"[{filename}] Processing monitor data...")
    dir_name, file_name = os.path.split(filename)
    monitor_filename = os.path.join(dir_name, file_name)
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
                    if time < 0:
                        continue
                    if process_name not in process_usages:
                        process_usages[process_name] = {
                            "cpu": [],
                            "mem": [],
                        }
                    while time >= len(process_usages[process_name]["cpu"]):
                        process_usages[process_name]["cpu"].append(np.nan)
                        process_usages[process_name]["mem"].append(np.nan)
                    process_usages[process_name]["cpu"][time] = process_info["cpu"]
                    process_usages[process_name]["mem"][time] = process_info["mem"]
        except json.JSONDecodeError:
            if "process not found: redis-leveldb" in line:
                break
            pass

    print(f"[{filename}] Finalizing resource usage data...")
    for process_usage in process_usages.values():
        cpu = process_usage["cpu"][args.start:args.end]
        mem = process_usage["mem"][args.start:args.end]
        mem = [m / 1024.0 / 1024.0 if not np.isnan(m) else np.nan for m in mem]
        process_usage["cpu"] = cpu
        process_usage["mem"] = mem
    for process_name, process_usage in process_usages.items():
        print(f"{process_name}[{args.start}:{args.end}]: avg cpu: {np.nanmean(process_usage['cpu'])}, avg mem: {np.nanmean(process_usage['mem'])}")

cnt = len(args.filenames)
for filename in args.filenames:
    process_monitor(filename)