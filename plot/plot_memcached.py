import os 
from os import listdir
from os.path import isfile, join
import math
import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import sys
import argparse

matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

def plot_single(ax, x_points, y_points1, y_points2, y_points2_2, y_points3, arrival_rate, crash_time, title, show_xlabel = True, show_ylabel = True):
    color = 'tab:orange'
    if show_xlabel:
      ax.set_xlabel('Time (s)', fontsize=11)
    else:
      ax.set_xticklabels([])
    if show_ylabel:
      ax.set_ylabel('Throughput (rps)', fontsize=11)
    ax.plot(x_points, y_points1, color=color, linewidth=0, alpha=0.6)
    ax.tick_params(axis='y', labelcolor='black')
    ax.fill_between(x_points, y_points1, y_points2, color=color, alpha=0.5, label="MySQL")
    
    color = 'tab:green'
    ax.plot(x_points, y_points2_2, color=color, linewidth=0, alpha=0.6)
    ax.fill_between(x_points, y_points2_2, 0, color=color, alpha=0.5, label="Memcached", hatch="//", edgecolor='black', linewidth=0.5)
    
    color = 'tab:purple'
    ax.plot(x_points, y_points2, color=color, linewidth=0, alpha=0.6)
    ax.fill_between(x_points, y_points2, y_points2_2, color=color, alpha=0.5, label="Memcached (Stale)", hatch="||", edgecolor='black', linewidth=0.5)
    
    color = 'tab:red'
    ax.plot(x_points, y_points3, color=color, linewidth=0, alpha=0.6)
    ax.fill_between(x_points, y_points3, y_points1, color=color, alpha=0.5, label="Timeout", hatch="\\\\", edgecolor='black', linewidth=0.5)
    
    ax.axvline(x=crash_time, color='red', dashes=(2, 2), label='Crash', linewidth=2)

    ax.set_ylim(ymin=0, ymax=int(arrival_rate))
    if title == '(c) Checkpoint (every 30s)':
        ax.set_title(title, y=-0.4, x=0, ha="left")
    else:
        ax.set_title(title, y=-0.25, x=0, ha="left")
    return ax

def plot(results, arrival_rate, crash_time, output):
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(8, 4.7))  # 3 rows, 1 column
    
    titles = ['(a) Vanilla', '(b) LiteLib', '(c) Checkpoint (every 30s)']
    axes = [ax1, ax2, ax3]
    
    for idx, (result, ax, title) in enumerate(zip(results, axes, titles)):
        plot_single(ax, 
                   result['time_points'],
                   result['successful_throughput'],
                   result['hit_throughput'],
                   result['true_hit_throughput'],
                   result['job_completions'],
                   arrival_rate,
                   crash_time,
                   title,
                   show_xlabel = idx == len(axes) - 1,
                   show_ylabel = idx == 1)
        
        if idx == 0:  # Only add legend to the first plot
            ax.legend(loc='lower left', bbox_to_anchor=(0., 0.92), frameon=False, fontsize=11, ncol=5, columnspacing=0.5, handletextpad=0.5)

    
    fig.tight_layout()
    fig.subplots_adjust(hspace=0.3)
    plt.savefig(output, bbox_inches='tight')

def process_file(file_name):
    ns_in_a_sec = 1000000000
    num_seconds = -1
    hit_rates = [0] * 100000
    error_rates = [0] * 100000
    stale_rates = [0] * 100000
    job_completions = [0] * 100000
    latency_per_second = [0] * 100000
    successful_latency = []
    
    with open(file_name) as file:
        first_line = file.readline()
        first_line = first_line.strip()
        experiment_start_time = int(first_line)
        for line in file:
            split_line = line.split(" ")
            stripped = [s.strip() for s in split_line]
            start_time = int(stripped[0])
            duration = int(stripped[1])
            end_time = (start_time + duration) - experiment_start_time
            cache_hits = int(stripped[2])
            errors = int(stripped[3])
            
            t_th_second = math.ceil(end_time/ns_in_a_sec)
            num_seconds = max( num_seconds , t_th_second) 
            hit_rates[ t_th_second ] += cache_hits # cache_hits will be either 0 or 1
            error_rates[ t_th_second ] += (errors == 1)
            stale_rates[ t_th_second ] += (errors == 2)
            job_completions[ t_th_second ]+= 1 # as each entry correspond to a job completion     
            latency_per_second[t_th_second]+=duration
            if (errors == 0):
                while (len(successful_latency) < t_th_second + 1):
                    successful_latency.append([])
                successful_latency[t_th_second].append(duration)

    total_time = args.total_time  # Get the maximum duration
    num_seconds = min(num_seconds, total_time)  # Limit to total_time

    hit_rates = hit_rates[0: num_seconds+1]
    error_rates = error_rates[0: num_seconds+1]
    stale_rates = stale_rates[0: num_seconds+1]
    latency_per_second = latency_per_second[0: num_seconds+1]
    job_completions = job_completions[0: num_seconds+1]
    successful_latency = successful_latency[0: num_seconds+1]
    time_points = [0] * (num_seconds + 1)

    for j in range (0, num_seconds+1): 
        if(job_completions[j]!= 0):
            hit_rates[j]/= job_completions[j]
            error_rates[j]/= job_completions[j]
            stale_rates[j]/= job_completions[j]
            latency_per_second[j]/= job_completions[j]
        time_points[j] = j 

    # print("max cache hit rate: " + str(max( hit_rates)))
    # print("max cache hit rate index : " + str(hit_rates.index(max(hit_rates))))
    # print("job completions : " + str(job_completions[1]))

    hit_rate_points  = np.array( hit_rates)
    error_rate_points = np.array( error_rates)
    stale_rate_points = np.array( stale_rates)
    time_points = np.array(time_points)
    latency_points = np.array(latency_per_second)

    job_completitions_points = np.array(job_completions)
    successful_throughput_points = job_completitions_points * (1 - error_rate_points)
    true_hit_throughput_points = job_completitions_points * (hit_rate_points - stale_rate_points)
    hit_throughput_points = job_completitions_points * hit_rate_points
    # error_throughput_points = job_completitions_points * error_rate_points
    # print(successful_latency)
    p99_latency_points = [(np.percentile(successful_latency[x], 99) if len(successful_latency[x]) > 0 else 0) for x in range(len(successful_latency))]
    # p95_latency_points = [(np.percentile(successful_latency[x], 95) if len(successful_latency[x]) > 0 else 0) for x in range(len(successful_latency))]
    avg_latency_points = [(np.mean(successful_latency[x]) if len(successful_latency[x]) > 0 else 0) for x in range(len(successful_latency))]

    return {
        'time_points': time_points,
        'successful_throughput': successful_throughput_points,
        'hit_throughput': hit_throughput_points,
        'true_hit_throughput': true_hit_throughput_points,
        'job_completions': job_completitions_points,
        'avg_latency': avg_latency_points,
        'p99_latency': p99_latency_points
    }

## helper functions end

parser = argparse.ArgumentParser(description="Process result files.")

parser.add_argument("filenames", nargs="+", help="The path to the result file(s)")
parser.add_argument(
    "-r", "--arrival_rate", type=int, default=400, help="arrival rate"
)
parser.add_argument(
    "-t", "--total_time", type=int, default=300, help="maximum duration"
)
parser.add_argument(
    "-o", "--output", type=str, default="memcached.pdf", help="output file name"
)

args = parser.parse_args()

if len(args.filenames) != 3:
    raise argparse.ArgumentTypeError("Invalid number of files. Expected 3 (full, checkpoint, lite).")

# Process all input files
results = []
for filename in args.filenames:
    results.append(process_file(filename))

crash_time = results[0]['time_points'][results[0]['job_completions'][10:].argmin()] + 10
print(f"Crash time: {crash_time}")

# Plot results from all files
plot(results, args.arrival_rate, crash_time, args.output)  # Note: arrival_rate needs to be defined

for i in range(len(results)):
    prefix = "full" if i == 0 else "lite" if i == 1 else "checkpoint"
    print(f"{prefix}: hit throughput avg before trigger {np.mean(results[i]['hit_throughput'][:crash_time])}, after trigger {np.mean(results[i]['hit_throughput'][crash_time:])}")
    print(f"{prefix}: min hit throughput after 2s {min(results[i]['hit_throughput'][2:])}")
    print(f"{prefix}: avg latency after trigger {np.mean(results[i]['avg_latency'][crash_time + 10:])}")
    print(f"{prefix}: avg latency before trigger {np.mean(results[i]['avg_latency'][10:crash_time-10])}")
    print(f"{prefix}: total number of true response after trigger {np.sum(results[i]['true_hit_throughput'][crash_time:])}")
    if i == 2:
        print(f"{prefix}: stale count: {np.sum(results[i]['hit_throughput'] - results[i]['true_hit_throughput'])}")
        print(f"{prefix}: stale rate 10s after trigger {np.mean((results[i]['hit_throughput'] - results[i]['true_hit_throughput'])[crash_time + 10:])}")

    def rolling_average(a, n):
        ret = np.cumsum(a, dtype=float)
        ret[n:] = ret[n:] - ret[:-n]
        return ret[n - 1:] / n
    
    window_size = 5
        
    rolling_avg = rolling_average(results[i]['hit_throughput'] / args.arrival_rate, window_size)[crash_time+1:]
    if np.max(rolling_avg) < 0.9:
        print(f"{prefix}: hit throughput never reaches 90% of arrival rate")
        print(f"{prefix}: hit throughput for the last {window_size}s: {np.max(results[i]['hit_throughput'][-window_size:])}")
    else:
        first_time = np.argmax(rolling_avg > 0.9) + crash_time + 1
        print(f"{prefix}: time to reach 90% of arrival rate: {first_time - crash_time}s")
