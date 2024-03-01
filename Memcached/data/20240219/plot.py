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
import json

ns_in_a_sec = 1000000000


def plot_performance(
    plot,
    # image_file_path,
    time_series,
    throughtput_ylim,
    latency_ylim,
    successful_throughput,
    hit_throughput,
    job_completions,
    avg_latency,
    p99_latency,
    hide_y_left=False,
    hide_y_right=False,
):
    # f, ax = plt.subplots()
    # plt.plot(x_points, y_points)
    # plt.plot(time_points, latency_points)
    # ax.set_ylim(ymin=0)
    # fig, ax1 = plot.subplots()
    ax1 = plot

    color = "tab:orange"
    ax1.set_xlabel("time (s)")
    if not hide_y_left:
        ax1.set_ylabel("throughput (rps)", color="black")
    ax1.plot(time_series, successful_throughput, color=color, linewidth=0, alpha=0.6)
    ax1.tick_params(axis="y", labelcolor="black")
    ax1.fill_between(
        time_series,
        successful_throughput,
        hit_throughput,
        color=color,
        alpha=0.2,
        label="MySQL",
    )
    color = "tab:blue"
    ax1.plot(time_series, hit_throughput, color=color, linewidth=0, alpha=0.6)
    ax1.fill_between(
        time_series, hit_throughput, 0, color=color, alpha=0.2, label="Memcached"
    )
    color = "tab:red"
    ax1.plot(time_series, job_completions, color=color, linewidth=0, alpha=0.6)
    ax1.fill_between(
        time_series,
        job_completions,
        successful_throughput,
        color=color,
        alpha=0.2,
        label="Error",
    )
    ax1.set_ylim(ymin=0, ymax=int(throughtput_ylim))
    # plt.arrow(x=10, y=0, dx=0, dy=5, width=.08, facecolor='red')
    # ax1.get_xaxis().set_visible(False)
    ax1.legend(
        bbox_to_anchor=(0, 1.02, 1, 0.2),
        loc="lower left",
        mode="expand",
        borderaxespad=0,
        ncol=2,
    )
    if hide_y_left:
        # ax1.get_yaxis().set_visible(False)
        ax1.get_legend().remove()

    ax2 = ax1.twinx()  # instantiate a second axes that shares the same x-axis
    if not hide_y_right:
        ax2.set_ylabel(
            "latency (ns)", color="black"
        )  # we already handled the x-label with ax1
    ax2.plot(
        time_series,
        avg_latency,
        "--",
        color="tab:pink",
        linewidth=2,
        alpha=1,
        label="avg latency",
    )
    ax2.plot(
        time_series,
        p99_latency,
        "--",
        color="tab:purple",
        linewidth=2,
        alpha=1,
        label="p99 latency",
    )
    # ax2.plot(x_points, y_points6, '--', color='tab:brown', linewidth = 2, alpha= 1, label = "p99 latency")
    ax2.set_ylim(ymin=0, ymax=latency_ylim)
    ax2.get_xaxis().set_visible(False)
    ax2.legend(
        bbox_to_anchor=(0, 1.02, 1, 0.2),
        loc="lower left",
        mode="expand",
        borderaxespad=0,
        ncol=2,
    )
    if hide_y_right:
        # ax2.get_yaxis().set_visible(False)
        ax2.get_legend().remove()

    # ax3 = ax1.twinx()
    # ax3.spines["right"].set_position(("outward", 40))
    # ax3.set_ylabel(
    #     "cpu usage (%)", color="black"
    # )  # we already handled the x-label with ax1
    # ax3.set_ylim(ymin=0, ymax=max(cpus) + 0.1)
    # ax3.plot(
    #     time_series, cpus, "-", color="tab:green", linewidth=2, alpha=1, label="cpu"
    # )

    # ax4 = ax1.twinx()
    # ax4.spines["right"].set_position(("outward", 100))
    # ax4.set_ylim(ymin=0, ymax=max(mems) + 0.1)
    # ax4.set_ylabel(
    #     "memory usage (%)", color="black"
    # )  # we already handled the x-label with ax1
    # ax4.plot(
    #     time_series, mems, "-", color="tab:olive", linewidth=2, alpha=1, label="mem"
    # )

    # fig.legend(
    #     bbox_to_anchor=(0, 1.02, 1, 0.2),
    #     loc="lower left",
    #     mode="expand",
    #     borderaxespad=0,
    #     ncol=3,
    # )
    # fig.tight_layout()  # otherwise the right y-label is slightly clipped

    # plt.savefig(image_file_path, bbox_inches="tight")


def plot_resources(
    plot,
    time_series,
    ylim,
    res_name,
    process_usages,
    hide_x = False,
    hide_y = False,
):
    ax1 = plot

    if not hide_x:
        ax1.set_xlabel("time (s)")
    if not hide_y:
        ax1.set_ylabel(f"{res_name} usage", color="black")
    for process_name, process_usage in process_usages.items():
        ax1.plot(
            time_series,
            process_usage[res_name],
            linewidth=2,
            alpha=1,
            label=process_name,
        )
    ax1.set_ylim(ymin=0, ymax=ylim)
    ax1.legend(
        bbox_to_anchor=(0, 1.02, 1, 0.2),
        loc="lower left",
        mode="expand",
        borderaxespad=0,
        ncol=1,
    )

def read_log_and_write_to_summary(log_file_name, summary_file_name):
    num_seconds = -1
    hit_rates = [
        0
    ] * 100000  # upper bound , assuuming experiment goes on for 1000 seconds
    error_rates = [0] * 100000
    job_completions = [0] * 100000
    latency_per_second = [0] * 100000
    successful_latency = []

    with open(log_file_name) as file:
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

            t_th_second = math.ceil(end_time / ns_in_a_sec)
            num_seconds = max(num_seconds, t_th_second)
            hit_rates[t_th_second] += cache_hits  # cache_hits will be either 0 or 1
            error_rates[t_th_second] += errors  # error will be either 0 or 1
            job_completions[
                t_th_second
            ] += 1  # as each entry correspond to a job completion
            latency_per_second[t_th_second] += duration
            if errors == 0:
                while len(successful_latency) < t_th_second + 1:
                    successful_latency.append([])
                successful_latency[t_th_second].append(duration)

    while len(successful_latency) < num_seconds + 2:
        successful_latency.append([])

    hit_rates = hit_rates[0 : num_seconds]
    error_rates = error_rates[0 : num_seconds]
    latency_per_second = latency_per_second[0 : num_seconds]
    job_completions = job_completions[0 : num_seconds]
    successful_latency = successful_latency[0 : num_seconds]
    time_points = [0] * (num_seconds)

    stats_file = open(summary_file_name, "w")
    for k in range(0, num_seconds):
        stats_file.write(
            str(job_completions[k])
            + " "
            + str(hit_rates[k])
            + " "
            + str(error_rates[k])
            + "\n"
        )

    for j in range(0, num_seconds):
        if job_completions[j] != 0:
            hit_rates[j] /= job_completions[j]
            error_rates[j] /= job_completions[j]
            latency_per_second[j] /= job_completions[j]
        time_points[j] = j

    # print("max cache hit rate: " + str(max( hit_rates)))
    # print("max cache hit rate index : " + str(hit_rates.index(max(hit_rates))))
    # print("job completions : " + str(job_completions[1]))

    hit_rate_points = np.array(hit_rates)
    error_rate_points = np.array(error_rates)
    time_points = np.array(time_points)
    latency_points = np.array(latency_per_second)
    job_completions_points = np.array(job_completions)
    successful_latency_points = np.array(successful_latency, dtype=object)
    successful_throughput_points = job_completions_points * (1 - error_rate_points)
    hit_throughput_points = job_completions_points * hit_rate_points
    p99_latency_points = [
        (
            np.percentile(successful_latency[x], 99)
            if len(successful_latency[x]) > 0
            else 0
        )
        for x in range(len(successful_latency))
    ]
    avg_latency_points = [
        (np.mean(successful_latency[x]) if len(successful_latency[x]) > 0 else 0)
        for x in range(len(successful_latency))
    ]

    return (
        num_seconds,
        hit_rate_points,
        error_rate_points,
        time_points,
        latency_points,
        job_completions_points,
        successful_latency_points,
        successful_throughput_points,
        hit_throughput_points,
        p99_latency_points,
        avg_latency_points,
    )


def read_monitor_log(filename, num_seconds):
    with open(filename, "r") as file:
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
        cpu = process_usage["cpu"][0 : num_seconds]
        mem = process_usage["mem"][0 : num_seconds]
        cpu = np.array(cpu)
        mem = np.array(mem) / 1024.0 / 1024.0
        process_usage["cpu"] = cpu
        process_usage["mem"] = mem
    ordered_process_usages = {}
    for process_name in sorted(process_usages.keys()):
        ordered_process_usages[process_name] = process_usages[process_name]
    return ordered_process_usages


if __name__ == "__main__":
    arrival_rate = int(sys.argv[1:][0])
    test_duration = int(sys.argv[1:][1])
    trigger_time = int(sys.argv[1:][2])
    prefix_number = int(sys.argv[1:][3])

    file_prefix = []
    num_seconds = []
    hit_rate = []
    error_rate = []
    time = []
    latency = []
    job_completions = []
    successful_latency = []
    successful_throughput = []
    hit_throughput = []
    p99_latency = []
    avg_latency = []
    process_usages = []
    for i in range(prefix_number):
        new_file_prefix = sys.argv[1:][4 + i]
        (
            new_num_seconds,
            new_hit_rate,
            new_error_rate,
            new_time,
            new_latency,
            new_job_completions,
            new_successful_latency,
            new_successful_throughput,
            new_hit_throughput,
            new_p99_latency,
            new_avg_latency,
        ) = read_log_and_write_to_summary(f"{new_file_prefix}.result.txt", f"{new_file_prefix}.summary.txt")
        new_process_usages = read_monitor_log(
            f"{new_file_prefix}.monitor.txt", new_num_seconds
        )
        file_prefix.append(new_file_prefix)
        num_seconds.append(new_num_seconds)
        hit_rate.append(new_hit_rate)
        error_rate.append(new_error_rate)
        time.append(new_time)
        latency.append(new_latency)
        job_completions.append(new_job_completions)
        successful_latency.append(new_successful_latency)
        successful_throughput.append(new_successful_throughput)
        hit_throughput.append(new_hit_throughput)
        p99_latency.append(new_p99_latency)
        avg_latency.append(new_avg_latency)
        process_usages.append(new_process_usages)

    latency_ylim = 1.1 * 1e9
    throughput_ylim = arrival_rate * 1.1
    fig, axs = plt.subplots(3, prefix_number, figsize=(5 * prefix_number, 15))
    plt.subplots_adjust(hspace=0.3, wspace=0.3)

    for i in range(prefix_number):
        axs[0, i].set_title(f"{file_prefix[i]}", y=1.2)
        plot_performance(
            # "{left_file_prefix}.png",
            axs[0, i],
            time[i],
            throughput_ylim,
            latency_ylim,
            successful_throughput[i],
            hit_throughput[i],
            job_completions[i],
            avg_latency[i],
            p99_latency[i],
            hide_y_left=(i != 0),
            hide_y_right=(i != prefix_number - 1),
        )
        cpus_ylim = 40
        plot_resources(
            axs[1, i],
            time[i],
            cpus_ylim,
            "cpu",
            process_usages[i],
            hide_x=True,
            hide_y=(i != 0),
        )
        mems_ylim = 300
        plot_resources(
            axs[2, i],
            time[i],
            mems_ylim,
            "mem",
            process_usages[i],
        )

    plt.savefig(f"comparison.png", bbox_inches="tight")

    for i in range(prefix_number):
        print(f"{file_prefix[i]}: hit throughput avg before trigger {np.mean(hit_throughput[i][:trigger_time])}, after trigger {np.mean(hit_throughput[i][trigger_time:])}")
        print(f"{file_prefix[i]}: min hit throughput after 2s {min(hit_throughput[i][2:])}")
        for process in process_usages[i]:
            print(f"{file_prefix[i]}({process}): CPU Avg before trigger {np.mean(process_usages[i][process]['cpu'][:trigger_time])}, after trigger {np.mean(process_usages[i][process]['cpu'][trigger_time:])}")
            print(f"{file_prefix[i]}({process}): Mem Avg before trigger {np.mean(process_usages[i][process]['mem'][:trigger_time])}, after trigger {np.mean(process_usages[i][process]['mem'][trigger_time:])}")

    # print(f"{left_file_prefix}: hit throughput avg before trigger {np.mean(left_hit_throughput[:trigger_time])}, after trigger {np.mean(left_hit_throughput[trigger_time:])}")
    # print(f"{file_prefix}: hit throughput avg before trigger {np.mean(hit_throughput[:trigger_time])}, after trigger {np.mean(hit_throughput[trigger_time:])}")
    # print("")

    # print(f"{left_file_prefix}: min hit throughput after 2s {min(left_hit_throughput[2:])}")
    # print(f"{file_prefix}: min hit throughput after 2s {min(hit_throughput[2:])}")
    # print("")

    # for process in left_process_usages:
    #     print(f"{left_file_prefix}({process}): CPU Avg before trigger {np.mean(left_process_usages[process]['cpu'][:trigger_time])}, after trigger {np.mean(left_process_usages[process]['cpu'][trigger_time:])}")
    #     print(f"{left_file_prefix}({process}): Mem Avg before trigger {np.mean(left_process_usages[process]['mem'][:trigger_time])}, after trigger {np.mean(left_process_usages[process]['mem'][trigger_time:])}")
    # for process in process_usages:
    #     print(f"{file_prefix}({process}): CPU Avg before trigger {np.mean(process_usages[process]['cpu'][:trigger_time])}, after trigger {np.mean(process_usages[process]['cpu'][trigger_time:])}")
    #     print(f"{file_prefix}({process}): Mem Avg before trigger {np.mean(process_usages[process]['mem'][:trigger_time])}, after trigger {np.mean(process_usages[process]['mem'][trigger_time:])}")

    # left_cpus_mean = np.mean(left_cpus)
    # left_mems_mean = np.mean(left_mems)
    # cpus_mean = np.mean(cpus)
    # mems_mean = np.mean(mems)
    # print(f"{left_file_prefix}: Avg CPU {left_cpus_mean}, Avg Mem {left_mems_mean}")
    # print(f"{file_prefix}: Avg CPU {cpus_mean}, Avg Mem {mems_mean}")
    # print(f"Relative: Avg CPU {cpus_mean / left_cpus_mean * 100}%, Avg Mem {mems_mean / left_mems_mean * 100}%")

    # print(f"{left_file_prefix}: avg hit throughput {np.mean(left_hit_throughput)}")
    # print(f"{file_prefix}: avg hit throughput {np.mean(hit_throughput)}")

    # left_polyfit = np.polyfit(left_time, left_successful_throughput, 1)
    # polyfit = np.polyfit(time, successful_throughput, 1)
    # print(f"{left_file_prefix} throughput - time polyfit: {left_polyfit}")
    # print(
    #     f"{file_prefix} throughput - time polyfit: {polyfit}"
    # )
    # print(f"Relative slope: {polyfit[0] / left_polyfit[0] * 100}%")
    # print(f"Relative intercept: {polyfit[1] / left_polyfit[1] * 100}%")

    # left_cpus_polyfit = np.polyfit(left_successful_throughput, left_cpus, 1)
    # cpus_polyfit = np.polyfit(successful_throughput, cpus, 1)
    # print(f"{left_file_prefix} CPU - throughput polyfit: {left_cpus_polyfit}")
    # print(
    #     f"{file_prefix} CPU - throughput polyfit: {cpus_polyfit}"
    # )
    # print(f"Relative CPU slope: {cpus_polyfit[0] / left_cpus_polyfit[0] * 100}%")
    # print(f"Relative CPU intercept: {cpus_polyfit[1] / left_cpus_polyfit[1] * 100}%")

    # left_mems_polyfit = np.polyfit(np.cumsum(left_successful_throughput), left_mems, 1)
    # mems_polyfit = np.polyfit(np.cumsum(successful_throughput), mems, 1)
    # print(f"{left_file_prefix} Mem - cumulative sum throughput polyfit: {left_mems_polyfit}")
    # print(
    #     f"{file_prefix} Mem - cumulative sum throughput polyfit: {mems_polyfit}"
    # )
    # print(f"Relative Mem slope: {mems_polyfit[0] / left_mems_polyfit[0] * 100}%")
    # print(f"Relative Mem intercept: {mems_polyfit[1] / left_mems_polyfit[1] * 100}%")
