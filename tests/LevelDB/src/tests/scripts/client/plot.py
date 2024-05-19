import json
import argparse
import math
import matplotlib.pyplot as plt
import numpy as np
import os


def plot_throughput(ax, stat, prefix):
    total_time = len(stat["cnt"])
    ax.fill_between(
        range(total_time),
        stat[prefix + "Success"],
        label="Success",
        alpha=0.5,
        color="tab:green",
    )
    base = stat[prefix + "Success"]
    ax.fill_between(
        range(total_time),
        base,
        base + stat[prefix + "Miss"],
        label="Miss",
        alpha=0.5,
        color="tab:orange",
    )
    base += stat[prefix + "Miss"]
    ax.fill_between(
        range(total_time),
        base,
        base + stat[prefix + "Timeout"],
        label="Timeout",
        alpha=0.5,
        color="tab:red",
    )
    base += stat[prefix + "Timeout"]
    ax.fill_between(
        range(total_time),
        base,
        base + stat[prefix + "Error"],
        label="Error",
        alpha=0.5,
        color="tab:purple",
    )
    if np.max(stat[prefix + "TransactionError"]) > 0:
        base += stat[prefix + "Error"]
        ax.fill_between(
            range(len(stat[prefix + "TransactionError"])),
            base,
            base + stat[prefix + "TransactionError"],
            label="TransactionError",
            alpha=0.5,
            color="0",
        )
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(prefix + "Throughput")
    ax.set_xlim(0, total_time)
    ax.legend()


def plot_latency(ax, stat, type):
    total_time = len(stat["cnt"])
    ax.plot(stat[type] * 1000)
    ax.set_xlim(0, total_time)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(
        "Latency (ms) " + ("(Client)" if type == "agg_lat" else "(Server)")
    )


def plot_tries(ax, stat):
    total_time = len(stat["cnt"])
    ax.plot(stat["avg_tries"])
    ax.set_xlim(0, total_time)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Tries (Success)")


def plot_resource(ax, stat, res_name, ylim):
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
    total_time = math.floor(last_response_time - begin_time) + 1
    stat = {
        "cnt": [0] * total_time,
        "server_lat_sum": [0] * total_time,
        "agg_lat_sum": [0] * total_time,
        "tries": [0] * total_time,
        "Success": [0] * total_time,
        "Miss": [0] * total_time,
        "Timeout": [0] * total_time,
        "Error": [0] * total_time,
        "TransactionError": [0] * total_time,
        "ServerSuccess": [0] * total_time,
        "ServerMiss": [0] * total_time,
        "ServerError": [0] * total_time,
        "ServerTimeout": [0] * total_time,
        "ServerTransactionError": [0] * total_time,
    }
    for line in logs[i]:
        begin_index = math.floor(line["begin"] - logs[i][0]["begin"])
        stat["cnt"][begin_index] += 1
        if line["queries"][-1]["status"] == "Success":
            stat["agg_lat_sum"][begin_index] += (
                line["queries"][-1]["response"] - line["begin"]
            )
            stat["tries"][begin_index] += len(line["queries"])
        stat[line["queries"][-1]["status"]][begin_index] += 1
        for query in line["queries"]:
            request_index = math.floor(query["request"] - logs[i][0]["begin"])
            if query["status"] == "Success":
                stat["server_lat_sum"][request_index] += query["response"] - query["request"]
            stat["Server" + query["status"]][request_index] += 1
    stat["agg_lat"] = [0] * total_time
    stat["avg_tries"] = [0] * total_time
    stat["server_lat"] = [0] * total_time
    for j in range(total_time):
        stat["agg_lat"][j] = (
            stat["agg_lat_sum"][j] / stat["Success"][j]
            if stat["Success"][j] > 0
            else np.nan
        )
        stat["avg_tries"][j] = (
            stat["tries"][j] / stat["Success"][j] if stat["Success"][j] > 0 else np.nan
        )
        stat["server_lat"][j] = (
            stat["server_lat_sum"][j] / stat["ServerSuccess"][j]
            if stat["ServerSuccess"][j] > 0
            else np.nan
        )
    for array in stat:
        stat[array] = np.array(stat[array])
    stats.append(stat)

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
                            "cpu": [0] * 100000,
                            "mem": [0] * 100000,
                        }
                    process_usages[process_name]["cpu"][time] = process_info["cpu"]
                    process_usages[process_name]["mem"][time] = process_info["mem"]
        except json.JSONDecodeError:
            pass
    for process_usage in process_usages.values():
        cpu = process_usage["cpu"][0 : len(stats[i]["cnt"])]
        mem = process_usage["mem"][0 : len(stats[i]["cnt"])]
        cpu = np.array(cpu)
        mem = np.array(mem) / 1024.0 / 1024.0
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

fig, axs = plt.subplots(7, cnt, figsize=(5 * cnt, 28))
plt.subplots_adjust(hspace=0.3, wspace=0.3)
for i in range(cnt):
    axs[0, i].set_title(args.filenames[i][:-6], y=1.2)
    plot_throughput(axs[0, i], stats[i], "")
    plot_latency(axs[1, i], stats[i], "agg_lat")
    plot_tries(axs[2, i], stats[i])
    plot_throughput(axs[3, i], stats[i], "Server")
    plot_latency(axs[4, i], stats[i], "server_lat")
    plot_resource(axs[5, i], stats[i], "cpu", cpu_ylim * 1.1)
    plot_resource(axs[6, i], stats[i], "mem", mem_ylim * 1.1)
plt.savefig(f"leveldb.png", bbox_inches="tight")
