import json
import argparse
import math
import matplotlib.pyplot as plt
import numpy as np
import os
import re
from datetime import datetime


def annotate_time_points(ax, stat):
    type = [
        ("crash_time", "crash", "red", (2, 2, 2, 2)),
        ("reboot_time", "rebooted", "orange", (3, 2, 2, 0)),
        ("replay_time", "replayed", "green", (1, 2, 2, 2)),
    ]
    for t, label, c, dash in type:
        if stat[t] is not np.nan:
            ax.axvline(x=stat[t], color=c, dashes=dash, label=label)


def plot_throughput(ax, stat, prefix):
    annotate_time_points(ax, stat)
    type = [
        ("Success", "tab:green"),
        ("Miss", "tab:orange"),
        ("Timeout", "tab:red"),
        ("Error", "tab:purple"),
        ("TransactionError", "0"),
    ]
    total_time = len(stat["cnt"])
    base = np.zeros(total_time)
    for t, c in type:
        next_base = base + stat[prefix + t]
        if (next_base != base).any():
            ax.fill_between(
                range(total_time),
                base,
                next_base,
                label=t,
                alpha=0.5,
                color=c,
            )
            base = next_base
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(prefix + " Throughput")
    ax.set_xlim(0, total_time)
    ax.legend()


def plot_latency(ax, stat, type, ylabel):
    annotate_time_points(ax, stat)
    total_time = len(stat["cnt"])
    ax.plot(stat[type] * 1000)
    ax.set_xlim(0, total_time)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(ylabel + " (ms)")


def plot_tries(ax, stat):
    annotate_time_points(ax, stat)
    total_time = len(stat["cnt"])
    ax.plot(stat["avg_tries"])
    ax.set_xlim(0, total_time)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Tries (Success)")


def plot_resource(ax, stat, res_name, ylim):
    annotate_time_points(ax, stat)
    total_time = len(stat["cnt"])
    for process_name, process_usage in stat["resource"].items():
        ax.plot(
            process_usage[res_name],
            linewidth=2,
            alpha=1,
            label=process_name,
        )
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(f"{res_name} usage" + (" (%)" if res_name == "cpu" else " (MB)"))
    ax.legend(
        bbox_to_anchor=(0, 1.02, 1, 0.2),
        loc="lower left",
        mode="expand",
        borderaxespad=0,
        ncol=1,
    )
    ax.set_xlim(0, total_time)
    ax.set_ylim(0, ylim)


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


begin_time = 0


def get_index(time):
    return math.floor(time - begin_time)


def get_timestamp_in_the_end_of_a_line(line):
    match = re.search(r"\b(\d+)\b$", line)
    if match:
        number = match.group(1)
        return float(number) / 1e9 - begin_time
    return np.nan


parser = argparse.ArgumentParser(description="Process JSON files.")

parser.add_argument("-f", "--filenames", nargs="+", help="The path to the JSON file(s)")

args = parser.parse_args()

cnt = len(args.filenames)
for filename in args.filenames:
    if not filename.endswith(".jsonl"):
        raise argparse.ArgumentTypeError(
            f"Invalid file type: {filename}. Expected a '.jsonl' file."
        )

logs = []
for i in range(cnt):
    with open(args.filenames[i], "r") as f:
        data = json.load(f)
        for line in data:
            line["begin"] = line["begin"]["secs"] + line["begin"]["nanos"] / 1e9
            for query in line["queries"]:
                query["request"] = (
                    query["request"]["secs"] + query["request"]["nanos"] / 1e9
                )
                query["response"] = (
                    query["response"]["secs"] + query["response"]["nanos"] / 1e9
                )
        logs.append(data)

stats = []
for i in range(cnt):
    begin_time = np.min([line["begin"] for line in logs[i]])
    last_response_time = np.max(
        [query["response"] for line in logs[i] for query in line["queries"]]
    )
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
        "ClientTransactionError": np.zeros(total_time),
        "ServerSuccess": np.zeros(total_time),
        "ServerMiss": np.zeros(total_time),
        "ServerError": np.zeros(total_time),
        "ServerTimeout": np.zeros(total_time),
        "ServerTransactionError": np.zeros(total_time),
        "lock_wait_time": [[] for _ in range(total_time)],
    }
    for line in logs[i]:
        begin_index = get_index(line["begin"])
        stat["cnt"][begin_index] += 1
        if line["queries"][-1]["status"] == "Success":
            stat["agg_lat_list"][begin_index].append(
                line["queries"][-1]["response"] - line["begin"],
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
    stat["avg_agg_lat"] = mean2d(stat["agg_lat_list"])
    stat["p95_agg_lat"] = p2d(stat["agg_lat_list"], 95)
    stat["avg_server_lat"] = mean2d(stat["server_lat_list"])
    stat["p95_server_lat"] = p2d(stat["server_lat_list"], 95)
    stat["avg_tries"] = mean2d(stat["tries"])
    stat["avg_lock_wait_time"] = mean2d(stat["lock_wait_time"])
    stats.append(stat)

for i in range(cnt):
    log_file = args.filenames[i][:-6] + ".log"
    stat = stats[i]
    if not os.path.exists(log_file):
        print(f"Log file {log_file} does not exist, won't plot special timestamps")
        stat["crash_time"] = np.nan
        stat["reboot_time"] = np.nan
        stat["replay_time"] = np.nan
    else:
        begin_time = np.min([line["begin"] for line in logs[i]])
        with open(log_file, "r") as f:
            lines = f.readlines()
        for line in lines:
            if "ntering emergency mode" in line:
                stat["crash_time"] = get_timestamp_in_the_end_of_a_line(line)
            if "Exiting emergency mode" in line:
                stat["reboot_time"] = get_timestamp_in_the_end_of_a_line(line)
            if "Exited emergency mode" in line:
                stat["replay_time"] = get_timestamp_in_the_end_of_a_line(line)
        print(
            f"Case: {args.filenames[i][:-6]}, crash time: {stat['crash_time']}, reboot time: {stat['reboot_time']}, replay time: {stat['replay_time']}"
        )

cpu_ylim = 0
mem_ylim = 0

for i in range(cnt):
    dir_name, file_name = os.path.split(args.filenames[i])
    args.filenames[i] = os.path.join(dir_name, "monitor." + file_name)
    if not args.filenames[i].endswith(".jsonl"):
        raise argparse.ArgumentTypeError(
            f"Invalid file type: {args.filenames[i]}. Expected a '.jsonl' file."
        )
    with open(args.filenames[i], "r") as file:
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
                            "cpu": np.zeros(100000),
                            "mem": np.zeros(100000),
                        }
                    process_usages[process_name]["cpu"][time] = process_info["cpu"]
                    process_usages[process_name]["mem"][time] = process_info["mem"]
        except json.JSONDecodeError:
            pass
    for process_usage in process_usages.values():
        cpu = process_usage["cpu"][0 : len(stats[i]["cnt"])]
        mem = process_usage["mem"][0 : len(stats[i]["cnt"])]
        mem = mem / 1024.0 / 1024.0
        process_usage["cpu"] = cpu
        process_usage["mem"] = mem
        cpu_ylim = max(cpu_ylim, np.max(process_usage["cpu"]))
        mem_ylim = max(mem_ylim, np.max(process_usage["mem"]))
    ordered_process_usages = {}
    ordered_process_usages["redis-leveldb"] = process_usages["redis-leveldb"]
    for process_name in sorted(process_usages.keys()):
        if process_name != "redis-leveldb":
            ordered_process_usages[process_name] = process_usages[process_name]
    stats[i]["resource"] = ordered_process_usages

fig, axs = plt.subplots(10, cnt, figsize=(5 * cnt, 40))
plt.subplots_adjust(hspace=0.3, wspace=0.3)
for i in range(cnt):
    axs[0, i].set_title(args.filenames[i][:-6], y=1.2)
    plot_throughput(axs[0, i], stats[i], "Client")
    plot_latency(axs[1, i], stats[i], "avg_agg_lat", "Avg Client Latency")
    plot_latency(axs[2, i], stats[i], "p95_agg_lat", "95% Client Latency")
    plot_tries(axs[3, i], stats[i])
    plot_throughput(axs[4, i], stats[i], "Server")
    plot_latency(axs[5, i], stats[i], "avg_server_lat", "Avg Server")
    plot_latency(axs[6, i], stats[i], "p95_server_lat", "95% Server")
    plot_resource(axs[7, i], stats[i], "cpu", cpu_ylim * 1.1)
    plot_resource(axs[8, i], stats[i], "mem", mem_ylim * 1.1)
    plot_latency(axs[9, i], stats[i], "avg_lock_wait_time", "Avg Lock Wait Time")
plt.savefig(f"leveldb.png", bbox_inches="tight")
