import json
import argparse
import math
import matplotlib.pyplot as plt
import numpy as np

def plot_throughput(ax, stat):
  ax.fill_between(range(len(stat["Success"])), stat["Success"], label="Success", alpha=0.5, color="tab:green")
  base = stat["Success"]
  ax.fill_between(range(len(stat["Miss"])), base, base + stat["Miss"], label="Miss", alpha=0.5, color="tab:orange")
  base += stat["Miss"]
  ax.fill_between(range(len(stat["Timeout"])), base, base + stat["Timeout"], label="Timeout", alpha=0.5, color="tab:red")
  base += stat["Timeout"]
  ax.fill_between(range(len(stat["Error"])), base, base + stat["Error"], label="Error", alpha=0.5, color="tab:purple")
  # ax.plot(stat["cnt"], label="Total")
  # ax.plot(stat["Success"], label="Success")
  # ax.plot(stat["Miss"], label="Miss")
  # ax.plot(stat["Timeout"], label="Timeout")
  # ax.plot(stat["Error"], label="Error")
  ax.set_xlabel("Time (s)")
  ax.set_ylabel("Throughput")
  ax.legend()

def plot_latency(ax, stat):
  ax.plot(stat["avg_lat"])
  ax.set_xlabel("Time (s)")
  ax.set_ylabel("Latency (s) (Success)")

def plot_tries(ax, stat):
  ax.plot(stat["avg_tries"])
  ax.set_xlabel("Time (s)")
  ax.set_ylabel("Tries (Success)")

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
      line["end"] = line["end"]["secs"] + line["end"]["nanos"] / 1e9
    sorted(data, key=lambda x: x["begin"])
    logs.append(data)

stats = []
for i in range(cnt):
  stat = {"cnt": [], "lat": [], "tries": [], "Success": [], "Miss": [], "Timeout": [], "Error": []}
  for line in logs[i]:
    index = math.floor(line["begin"] - logs[i][0]["begin"])
    if len(stat["cnt"]) < index + 1:
      stat["cnt"] += [0] * (index + 1 - len(stat["cnt"]))
      stat["lat"] += [0] * (index + 1 - len(stat["lat"]))
      stat["tries"] += [0] * (index + 1 - len(stat["tries"]))
      stat["Success"] += [0] * (index + 1 - len(stat["Success"]))
      stat["Miss"] += [0] * (index + 1 - len(stat["Miss"]))
      stat["Timeout"] += [0] * (index + 1 - len(stat["Timeout"]))
      stat["Error"] += [0] * (index + 1 - len(stat["Error"]))
    stat["cnt"][index] += 1
    if line["status"] == "Success":
      stat["lat"][index] += line["end"] - line["begin"]
      stat["tries"][index] += line["tries"]
    stat[line["status"]][index] += 1
  stat["avg_lat"] = [0] * len(stat["cnt"])
  stat["avg_tries"] = [0] * len(stat["cnt"])
  for j in range(len(stat["Success"])):
    stat["avg_lat"][j] = stat["lat"][j] / stat["Success"][j] if stat["Success"][j] > 0 else 1
    stat["avg_tries"][j] = stat["tries"][j] / stat["Success"][j] if stat["Success"][j] > 0 else 11
  for array in stat:
    stat[array] = np.array(stat[array])
  stats.append(stat)

fig, axs = plt.subplots(3, cnt, figsize=(5 * cnt, 10))
plt.subplots_adjust(hspace=0.3, wspace=0.3)
for i in range(cnt):
  axs[0, i].set_title(filename[:-6], y=1.2)
  plot_throughput(axs[0, i], stats[i])
  plot_latency(axs[1, i], stats[i])
  plot_tries(axs[2, i], stats[i])
plt.savefig(f"leveldb.png", bbox_inches="tight")