import os
import re
import json
import numpy as np
import argparse
import matplotlib.pyplot as plt


def get_experiment_numbers():
    files = [f for f in os.listdir('.') if f.startswith('monitor.') and f.endswith('.jsonl')]
    numbers = []
    for f in files:
        match = re.search(r'monitor\.[a-z]+-(\d+)\.jsonl', f)
        if match:
            numbers.append(int(match.group(1)))
    return sorted(list(set(numbers)))

def get_cpu_usage(monitor_filename, total_time):
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
                            "cpu": np.full(total_time + 1, np.nan),
                            "mem": np.full(total_time + 1, np.nan),
                        }
                    if time <= total_time:
                        process_usages[process_name]["cpu"][time] = process_info["cpu"]
                        process_usages[process_name]["mem"][time] = process_info["mem"]
        except json.JSONDecodeError:
            pass
    for process_usage in process_usages.values():
        cpu = process_usage["cpu"][5 : total_time - 5] # drop the first 5 and the last 5
        mem = process_usage["mem"][5 : total_time - 5]
        mem = mem / 1024.0 / 1024.0
        process_usage["cpu"] = cpu
        process_usage["mem"] = mem
    ordered_process_usages = {}
    ordered_process_usages["redis-leveldb"] = process_usages["redis-leveldb"]
    for process_name in sorted(process_usages.keys()):
        if process_name != "redis-leveldb":
            ordered_process_usages[process_name] = process_usages[process_name]
    return ordered_process_usages

def get_avg_cpu_usage(process_usages):
    return np.nanmean(process_usages["cpu"])

parser = argparse.ArgumentParser(description="Process monitor files.")
parser.add_argument("-t", "--total-time", type=int, required=True, help="Total time in seconds")
args = parser.parse_args()

experiment_numbers = get_experiment_numbers()
lite_cpu_usages = []
full_cpu_usages = []
vanilla_cpu_usages = []

for number in experiment_numbers:
    lite = get_cpu_usage(f"monitor.lite-{number}.jsonl", args.total_time)
    full_cpu_usages.append(get_avg_cpu_usage(lite["redis-leveldb"]))
    lite_cpu_usages.append(get_avg_cpu_usage(lite["LiteLevelDB"]))
    vanilla = get_cpu_usage(f"monitor.full-{number}.jsonl", args.total_time)
    vanilla_cpu_usages.append(get_avg_cpu_usage(vanilla["redis-leveldb"]))

x = np.arange(len(experiment_numbers))
width = 0.35

fig, ax = plt.subplots(figsize=(10, 6))

# Create stacked bars
ax.bar(x - width/2, vanilla_cpu_usages, width, label='the full version w/o LiteSys', color='tab:green', alpha=0.5)
ax.bar(x + width/2, full_cpu_usages, width, label='the full version w/ LiteSys', color='tab:blue', alpha=0.5)
ax.bar(x + width/2, lite_cpu_usages, width, bottom=full_cpu_usages, label='the lite version', color='tab:orange', alpha=0.5)

# Add the ratio curve
combined_cpu = np.array(full_cpu_usages) + np.array(lite_cpu_usages)
ratio = combined_cpu / np.array(vanilla_cpu_usages) - 1
ax2 = ax.twinx()  # Create a second y-axis
ax2.plot(x, ratio, marker='o', label='LiteSys CPU Usage Overhead', color='tab:red')
ax2.set_ylabel('CPU Usage Overhead')
ax2.set_ylim(0, 1)

# Update legend to include both axes
lines1, labels1 = ax.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax2.legend(lines1 + lines2, labels1 + labels2, loc='upper left')

# Customize the plot
ax.set_ylabel('CPU Usage (%)')
ax.set_xlabel('Request Per Second')
# ax.set_title('CPU Usage Comparison')
ax.set_xticks(x)
ax.set_xticklabels(experiment_numbers)
ax.legend()

plt.tight_layout()
plt.savefig('leveldb_cpu_usage_overhead.png')
plt.savefig('leveldb_cpu_usage_overhead.pdf')
plt.close()


print(ratio)
print(lite_cpu_usages)
print(full_cpu_usages)
print(vanilla_cpu_usages)
