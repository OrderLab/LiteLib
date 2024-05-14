import json
import argparse
import math
import matplotlib.pyplot as plt
import numpy as np
import os

def plot_throughput(ax, stat):
  ax.fill_between(range(len(stat["Success"])), stat["Success"], label="Success", alpha=0.5, color="tab:green")
  base = stat["Success"]
  ax.fill_between(range(len(stat["Miss"])), base, base + stat["Miss"], label="Miss", alpha=0.5, color="tab:orange")
  base += stat["Miss"]
  ax.fill_between(range(len(stat["Timeout"])), base, base + stat["Timeout"], label="Timeout", alpha=0.5, color="tab:red")
  base += stat["Timeout"]
  ax.fill_between(range(len(stat["Error"])), base, base + stat["Error"], label="Error", alpha=0.5, color="tab:purple")
  if np.max(stat["TransactionError"]) > 0:
    base += stat["Error"]
    ax.fill_between(range(len(stat["TransactionError"])), base, base + stat["TransactionError"], label="TransactionError", alpha=0.5, color="0")
  # ax.plot(stat["cnt"], label="Total")
  # ax.plot(stat["Success"], label="Success")
  # ax.plot(stat["Miss"], label="Miss")
  # ax.plot(stat["Timeout"], label="Timeout")
  # ax.plot(stat["Error"], label="Error")
  ax.set_xlabel("Time (s)")
  ax.set_ylabel("Throughput")
  ax.legend()

def plot_latency(ax, stat, type):
  ax.plot(stat[type] * 1000)
  ax.set_xlabel("Time (s)")
  ax.set_ylabel("Latency (ms) " + ("(EndToEnd)" if type == "agg_lat" else "(SuccessResp)"))

def plot_tries(ax, stat):
  ax.plot(stat["avg_tries"])
  ax.set_xlabel("Time (s)")
  ax.set_ylabel("Tries (Success)")

def plot_resource(
  ax,
  stat,
  res_name,
  ylim
):
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
  ax.set_ylim(0, ylim)

parser = argparse.ArgumentParser(description='Process JSON files.')

parser.add_argument('-f', '--filenames', nargs='+', help='The path to the JSON file(s)')

args = parser.parse_args()

cnt = len(args.filenames)
for filename in args.filenames:
  if not filename.endswith('.jsonl'):
    raise argparse.ArgumentTypeError(f"Invalid file type: {filename}. Expected a '.jsonl' file.")

logs = []
for i in range(cnt):
  with open(args.filenames[i], 'r') as f:
    data = json.load(f)
    for line in data:
      line["begin"] = line["begin"]["secs"] + line["begin"]["nanos"] / 1e9
      line["last_request_time"] = line["last_request_time"]["secs"] + line["last_request_time"]["nanos"] / 1e9
      line["last_response_time"] = line["last_response_time"]["secs"] + line["last_response_time"]["nanos"] / 1e9
    sorted(data, key=lambda x: x["begin"])
    logs.append(data)

stats = []
for i in range(cnt):
  stat = {"cnt": [], "suc_lat_sum": [], "agg_lat_sum": [], "tries": [], "Success": [], "Miss": [], "Timeout": [], "Error": [], "TransactionError": []}
  for line in logs[i]:
    index = math.floor(line["last_response_time"] - logs[i][0]["last_response_time"])
    if len(stat["cnt"]) < index + 1:
      stat["cnt"] += [0] * (index + 1 - len(stat["cnt"]))
      stat["suc_lat_sum"] += [0] * (index + 1 - len(stat["suc_lat_sum"]))
      stat["agg_lat_sum"] += [0] * (index + 1 - len(stat["agg_lat_sum"]))
      stat["tries"] += [0] * (index + 1 - len(stat["tries"]))
      stat["Success"] += [0] * (index + 1 - len(stat["Success"]))
      stat["Miss"] += [0] * (index + 1 - len(stat["Miss"]))
      stat["Timeout"] += [0] * (index + 1 - len(stat["Timeout"]))
      stat["Error"] += [0] * (index + 1 - len(stat["Error"]))
      stat["TransactionError"] += [0] * (index + 1 - len(stat["TransactionError"]))
    stat["cnt"][index] += 1
    if line["status"] == "Success":
      stat["suc_lat_sum"][index] += line["last_response_time"] - line["last_request_time"]
      stat["agg_lat_sum"][index] += line["last_response_time"] - line["begin"]
      stat["tries"][index] += line["tries"]
    stat[line["status"]][index] += 1
  stat["suc_lat"] = [0] * len(stat["cnt"])
  stat["agg_lat"] = [0] * len(stat["cnt"])
  stat["avg_tries"] = [0] * len(stat["cnt"])
  for j in range(len(stat["Success"])):
    stat["suc_lat"][j] = stat["suc_lat_sum"][j] / stat["Success"][j] if stat["Success"][j] > 0 else 5
    stat["agg_lat"][j] = stat["agg_lat_sum"][j] / stat["Success"][j] if stat["Success"][j] > 0 else 5
    stat["avg_tries"][j] = stat["tries"][j] / stat["Success"][j] if stat["Success"][j] > 0 else 11
  for array in stat:
    stat[array] = np.array(stat[array])
  stats.append(stat)

cpu_ylim = 0
mem_ylim = 0

for i in range(cnt):
  dir_name, file_name = os.path.split(args.filenames[i])
  args.filenames[i] = os.path.join(dir_name, "monitor." + file_name)
  if not args.filenames[i].endswith('.jsonl'):
    raise argparse.ArgumentTypeError(f"Invalid file type: {args.filenames[i]}. Expected a '.jsonl' file.")
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
                      process_usages[process_name] = {"cpu": [0] * 100000, "mem": [0] * 100000}
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
  ordered_process_usages['redis-leveldb'] = process_usages['redis-leveldb']
  for process_name in sorted(process_usages.keys()):
      if process_name != 'redis-leveldb':
          ordered_process_usages[process_name] = process_usages[process_name]
  stats[i]["resource"] = ordered_process_usages

fig, axs = plt.subplots(6, cnt, figsize=(5 * cnt, 25))
plt.subplots_adjust(hspace=0.3, wspace=0.3)
for i in range(cnt):
  axs[0, i].set_title(args.filenames[i][:-6], y=1.2)
  plot_throughput(axs[0, i], stats[i])
  plot_latency(axs[1, i], stats[i], "suc_lat")
  plot_latency(axs[2, i], stats[i], "agg_lat")
  plot_tries(axs[3, i], stats[i])
  plot_resource(axs[4, i], stats[i], "cpu", cpu_ylim * 1.1)
  plot_resource(axs[5, i], stats[i], "mem", mem_ylim * 1.1)
plt.savefig(f"leveldb.png", bbox_inches="tight")