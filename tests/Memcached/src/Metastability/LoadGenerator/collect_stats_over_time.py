import os 
from os import listdir
from os.path import isfile, join
import math
import matplotlib.pyplot as plt
import numpy as np
import sys
from datetime import datetime
import time
import re

kill_timeout_for_php = 1
# helper functions

def plot_data( x_points, y_points, file_with_image_extension):
    f, ax = plt.subplots(1)
    plt.plot(x_points, y_points)
    # plt.plot(time_points, latency_points)
    ax.set_ylim(ymin=0) 

    
    image_directory = "result_plots"
    current_directory = os.getcwd()
    final_image_directory = os.path.join(current_directory, r'result_plots')

    if not os.path.exists(final_image_directory):
        os.makedirs(final_image_directory)   

    image_file_path = join(image_directory, file_with_image_extension) # combining file name with directory
    plt.savefig( image_file_path, bbox_inches='tight')

def make_patch_spines_invisible(ax):
    ax.set_frame_on(True)
    ax.patch.set_visible(False)
    for sp in ax.spines.values():
        sp.set_visible(False)


def plot_multiple_data( x_points, y_points1, y_points2, y_points3, file_with_image_extension):
   # f, ax = plt.subplots()
   # plt.plot(x_points, y_points)
    # plt.plot(time_points, latency_points)
    #ax.set_ylim(ymin=0) 
    fig, ax1 = plt.subplots()
    
    color = 'tab:red'
    ax1.set_xlabel('time (s)')
    ax1.set_ylabel('latency(ns)', color='black')
    ax1.plot(x_points, y_points1, color=color, linewidth = 2 , alpha= 0.6, label = "latency")
    ax1.tick_params(axis='y', labelcolor='black')

    ax2 = ax1.twinx()  # instantiate a second axes that shares the same x-axis

    color = 'tab:blue'
    ax2.set_ylabel('hit rate/error rate', color='black')  # we already handled the x-label with ax1
    ax2.plot(x_points, y_points2, color=color, linewidth = 2, alpha= 0.6, label = "cache hit rate")
    color = 'tab:orange'
    ax2.plot(x_points, y_points3, '--', color=color, linewidth = 2, alpha= 0.6, label = "error rate")
    ax2.set_ylim(ymin = 0, ymax = 1)
    # plt.arrow(x=10, y=0, dx=0, dy=5, width=.08, facecolor='red')
    ax2.tick_params(axis='y', labelcolor='black')
    fig.legend(bbox_to_anchor=(0,1.02,1,0.2), loc="lower left",
                mode="expand", borderaxespad=0, ncol=3)
    fig.tight_layout()  # otherwise the right y-label is slightly clipped
     
    image_directory = "result_plots"
    current_directory = os.getcwd()
    final_image_directory = os.path.join(current_directory, r'result_plots')

    if not os.path.exists(final_image_directory):
        os.makedirs(final_image_directory)   

    image_file_path = join(image_directory, file_with_image_extension) # combining file name with directory
    plt.savefig( image_file_path, bbox_inches='tight')


def plot_multiple_data2( x_points, y_points1, y_points2, y_points2_2, y_points3,
                         y_points4, y_points5, file_with_image_extension, arrival_rate,
                         cpu_points, mem_points):
   # f, ax = plt.subplots()
   # plt.plot(x_points, y_points)
    # plt.plot(time_points, latency_points)
    #ax.set_ylim(ymin=0) 
    fig, ax1 = plt.subplots()

    color = 'tab:orange'
    ax1.set_xlabel('time (s)')
    ax1.set_ylabel('throughput(rps)', color='black')
    ax1.plot(x_points, y_points1, color=color, linewidth = 0 , alpha= 0.6)
    ax1.tick_params(axis='y', labelcolor='black')
    ax1.fill_between(x_points, y_points1, y_points2, color=color, alpha=0.2, label = "MySQL")
    color = 'tab:blue'
    ax1.plot(x_points, y_points2_2, color=color, linewidth = 0, alpha= 0.6)
    ax1.fill_between(x_points, y_points2_2, 0, color=color, alpha=0.2, label = "Memcached")
    color = 'tab:purple'
    ax1.plot(x_points, y_points2, color=color, linewidth = 0, alpha= 0.6)
    ax1.fill_between(x_points, y_points2, y_points2_2, color=color, alpha=0.2, label = "Stale Memcached")
    color = 'tab:red'
    ax1.plot(x_points, y_points3, color=color, linewidth = 0, alpha= 0.6)
    ax1.fill_between(x_points, y_points3, y_points1, color=color, alpha=0.2, label = "Error")
    ax1.set_ylim(ymin = 0, ymax = int(arrival_rate))
    # plt.arrow(x=10, y=0, dx=0, dy=5, width=.08, facecolor='red')

    ax2 = ax1.twinx()  # instantiate a second axes that shares the same x-axis
    ax2.set_ylabel('latency(ns)', color='black')  # we already handled the x-label with ax1
    ax2.plot(x_points, y_points4, '--', color='tab:pink', linewidth = 2, alpha= 1, label = "avg latency")
    ax2.plot(x_points, y_points5, '--', color='tab:purple', linewidth = 2, alpha= 1, label = "p99 latency")
    # ax2.plot(x_points, y_points6, '--', color='tab:brown', linewidth = 2, alpha= 1, label = "p99 latency")
    ax2.set_ylim(ymin = 0)

    # ax3 = ax1.twinx()
    # ax3.spines["right"].set_position(("outward", 40))
    # ax3.set_ylabel('cpu usage (%)', color='black')  # we already handled the x-label with ax1
    # ax3.set_ylim(ymin = 0, ymax = max(cpu_points) + 0.1)
    # ax3.plot(x_points, cpu_points, '-', color='tab:green', linewidth = 2, alpha= 1, label = "cpu")

    # ax4 = ax1.twinx()
    # ax4.spines["right"].set_position(("outward", 100))
    # ax4.set_ylim(ymin = 0, ymax = max(mem_points) + 0.1)
    # ax4.set_ylabel('memory usage (%)', color='black')  # we already handled the x-label with ax1
    # ax4.plot(x_points, mem_points, '-', color='tab:olive', linewidth = 2, alpha= 1, label = "mem")

    fig.legend(bbox_to_anchor=(0,1.02,1,0.2), loc="lower left",
                mode="expand", borderaxespad=0, ncol=3)
    fig.tight_layout()  # otherwise the right y-label is slightly clipped
     
    image_directory = "result_plots"
    current_directory = os.getcwd()
    final_image_directory = os.path.join(current_directory, r'result_plots')

    if not os.path.exists(final_image_directory):
        os.makedirs(final_image_directory)   

    image_file_path = join(image_directory, file_with_image_extension) # combining file name with directory
    plt.savefig( image_file_path, bbox_inches='tight')
    plt.savefig( image_file_path[:-4] + ".pdf", bbox_inches='tight')

## helper functions end





args_len = len(sys.argv[1:])

if(args_len != 9):        
        print("enter valid parameter, provide absolute file path for resultFile from TraceReplay, arrival_rate, alpha, trigger_size, test_duration, monitor_file, exp_type, today_date_time, read_write_ratio")
        exit()
    
file_name = sys.argv[1:][0]
arrival_rate = sys.argv[1:][1]
alpha = sys.argv[1:][2]
trigger_size = sys.argv[1:][3]
test_duration = sys.argv[1:][4]
monitor_file = sys.argv[1:][5]
exp_type = sys.argv[1:][6]
today_date_time = sys.argv[1:][7]
read_write_ratio = sys.argv[1:][8]
ns_in_a_sec = 1000000000  
num_seconds = -1
hit_rates = [0] * 100000 # upper bound , assuuming experiment goes on for 1000 seconds
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

while (len(successful_latency) < num_seconds + 2):
    successful_latency.append([])

hit_rates = hit_rates[0: num_seconds+1]
error_rates = error_rates[0: num_seconds+1]
stale_rates = stale_rates[0: num_seconds+1]
latency_per_second = latency_per_second[0: num_seconds +1]
job_completions = job_completions[0: num_seconds +1]
successful_latency = successful_latency[0: num_seconds +1]
time_points = [0] * (num_seconds + 1)



current_directory = os.getcwd()
stats_directory = "result_stats"
final_stats_directory = os.path.join(current_directory, r'result_stats')

if not os.path.exists(final_stats_directory):
   os.makedirs(final_stats_directory)
stats_file_name = "stats_ARV_RATE_{}_ALPHA_{}_DUR_{}_RW_RATIO_{}_TRIGGER_{}_DATE_TIME_{}_TMOUT_{}_EXP_{}.txt".format(arrival_rate, alpha, test_duration, read_write_ratio, trigger_size, today_date_time, kill_timeout_for_php, exp_type) 
stats_file_path = os.path.join( final_stats_directory, stats_file_name)

stats_file = open(stats_file_path, "w") 
for k in range(0, num_seconds + 1):
    stats_file.write( str(job_completions[k]) + " " +  str(hit_rates[k]) + " " + str(error_rates[k]) + " " + str(stale_rates[k]) + "\n")


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
A_file_with_image_extension =  f"a_img_ARV_RATE_{arrival_rate}_ALPHA_{alpha}_DUR_{test_duration}_RW_RATIO_{read_write_ratio}_TRIGGER_{trigger_size}_DATE_TIME_{today_date_time}_TMOUT_{kill_timeout_for_php}_EXP_{exp_type}.png"   
H_file_with_image_extension =  f"h_img_ARV_RATE_{arrival_rate}_ALPHA_{alpha}_DUR_{test_duration}_RW_RATIO_{read_write_ratio}_TRIGGER_{trigger_size}_DATE_TIME_{today_date_time}_TMOUT_{kill_timeout_for_php}_EXP_{exp_type}.png"   
L_file_with_image_extension =  f"l_img_ARV_RATE_{arrival_rate}_ALPHA_{alpha}_DUR_{test_duration}_RW_RATIO_{read_write_ratio}_TRIGGER_{trigger_size}_DATE_TIME_{today_date_time}_TMOUT_{kill_timeout_for_php}_EXP_{exp_type}.png"   
E_file_with_image_extension =  f"e_img_ARV_RATE_{arrival_rate}_ALPHA_{alpha}_DUR_{test_duration}_RW_RATIO_{read_write_ratio}_TRIGGER_{trigger_size}_DATE_TIME_{today_date_time}_TMOUT_{kill_timeout_for_php}_EXP_{exp_type}.png"   
C_file_with_image_extension = f"c_img_ARV_RATE_{arrival_rate}_ALPHA_{alpha}_DUR_{test_duration}_RW_RATIO_{read_write_ratio}_TRIGGER_{trigger_size}_DATE_TIME_{today_date_time}_TMOUT_{kill_timeout_for_php}_EXP_{exp_type}.png"
plot_data(time_points, hit_rate_points, H_file_with_image_extension)
plot_data(time_points, error_rate_points, E_file_with_image_extension)
plot_data(time_points, latency_points, L_file_with_image_extension)
plot_multiple_data(time_points, latency_points, hit_rate_points, error_rate_points, C_file_with_image_extension)

with open(monitor_file, 'r') as file:
    lines = file.readlines()
pattern = re.compile(r'time: (-?\d+), cpu: ([\d.]+), mem: ([\d.]+)')
cpus = [0] * 100000
mems = [0] * 100000
for line in lines:
    match = pattern.search(line)
    if match and int(match.group(1)) >= 0:
        cpus[int(match.group(1))] = float(match.group(2))
        mems[int(match.group(1))] = float(match.group(3))
cpus = cpus[0: num_seconds+1]
mems = mems[0: num_seconds+1]
job_completitions_points = np.array(job_completions)
successful_throughput_points = job_completitions_points * (1 - error_rate_points)
true_hit_throughput_points = job_completitions_points * (hit_rate_points - stale_rate_points)
hit_throughput_points = job_completitions_points * hit_rate_points
# error_throughput_points = job_completitions_points * error_rate_points
# print(successful_latency)
p99_latency_points = [(np.percentile(successful_latency[x], 99) if len(successful_latency[x]) > 0 else 0) for x in range(len(successful_latency))]
# p95_latency_points = [(np.percentile(successful_latency[x], 95) if len(successful_latency[x]) > 0 else 0) for x in range(len(successful_latency))]
avg_latency_points = [(np.mean(successful_latency[x]) if len(successful_latency[x]) > 0 else 0) for x in range(len(successful_latency))]
print(f"max mysql throughput: {max(successful_throughput_points - hit_throughput_points)}")
print(f"avg mysql throughput: {np.mean(successful_throughput_points - hit_throughput_points)}")
print(f"min mysql throughput: {min(successful_throughput_points - hit_throughput_points)}")
plot_multiple_data2(time_points, successful_throughput_points, hit_throughput_points, true_hit_throughput_points, job_completitions_points,
                    avg_latency_points, p99_latency_points, A_file_with_image_extension, arrival_rate, np.array(cpus), np.array(mems))
# store completions cache_hit_rate error_rate

