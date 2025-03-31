import json
import argparse
import matplotlib.pyplot as plt
import numpy as np
import os
from datetime import datetime
import sys


def annotate_time_points(ax, stat):
    type = [
        ("crash_time", "crash", "red", (2, 2, 2, 2)),
        ("reboot_time", "rebooted", "orange", (3, 2, 2, 0)),
        ("replay_time", "replayed", "green", (1, 2, 2, 2)),
    ]
    for t, label, c, dash in type:
        if not np.isnan(stat[t]):
            ax.axvline(x=stat[t], color=c, dashes=dash, label=label)


def plot_throughput(ax, stat, prefix, ylim, xlim):
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
            label = t
            if t == "Error":
                label = (
                    "Stale Data"
                    if np.isnan(stat["replay_time"])
                    else "AdmissionControl"
                )
            ax.fill_between(
                range(total_time),
                base,
                next_base,
                label=label,
                alpha=0.5,
                color=c,
            )
            base = next_base
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(prefix + " Throughput")
    ax.set_xlim(0, xlim)
    ax.set_ylim(0, ylim)
    ax.legend()


def plot_latency(ax, stat, type, ylabel, ylim, xlim):
    annotate_time_points(ax, stat)
    ax.plot(stat[type])
    ax.set_xlim(0, xlim)
    ax.set_ylim(0, ylim)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel(ylabel + " (ms)")


def plot_tries(ax, stat, ylim, xlim):
    annotate_time_points(ax, stat)
    ax.plot(stat["avg_tries"])
    ax.set_xlim(0, xlim)
    ax.set_ylim(0.8, ylim)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Tries (Success)")


def plot_resource(ax, stat, res_name, ylim, xlim):
    annotate_time_points(ax, stat)
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
        # bbox_to_anchor=(0, 1.02, 1, 0.2),
        loc="lower left",
        # mode="expand",
        # borderaxespad=0,
        ncol=1,
    )
    ax.set_xlim(0, xlim)
    ax.set_ylim(0, ylim)


parser = argparse.ArgumentParser(description="Process JSON files.")

parser.add_argument("-f", "--filenames", nargs="+", help="The path to the JSON file(s)")
parser.add_argument(
    "-t", "--total_time", type=int, default=7200, help="maximum duration"
)

args = parser.parse_args()

cnt = len(args.filenames)
for filename in args.filenames:
    if not filename.endswith(".json"):
        raise argparse.ArgumentTypeError(
            f"Invalid file type: {filename}. Expected a '.json' file."
        )

client_throughput_lim = np.nan
client_latency_lim = np.nan
tries_lim = np.nan
server_throughput_lim = np.nan
server_latency_lim = np.nan
lock_time_lim = np.nan
cpu_ylim = np.nan
mem_ylim = np.nan


stats = []
for i in range(cnt):
    stat_file = args.filenames[i]
    with open(stat_file, "r") as f:
        stat = json.load(f)

    stat_len = len(stat["cnt"])
    if stat_len > args.total_time:
        stat_len = args.total_time
        stat["cnt"] = stat["cnt"][:stat_len]
        stat["ClientSuccess"] = stat["ClientSuccess"][:stat_len]
        stat["ClientMiss"] = stat["ClientMiss"][:stat_len]
        stat["ClientTimeout"] = stat["ClientTimeout"][:stat_len]
        stat["ClientError"] = stat["ClientError"][:stat_len]
        stat["ClientTransactionError"] = stat["ClientTransactionError"][:stat_len]
        stat["ServerSuccess"] = stat["ServerSuccess"][:stat_len]
        stat["ServerMiss"] = stat["ServerMiss"][:stat_len]
        stat["ServerTimeout"] = stat["ServerTimeout"][:stat_len]
        stat["ServerError"] = stat["ServerError"][:stat_len]
        stat["ServerTransactionError"] = stat["ServerTransactionError"][:stat_len]
        stat["avg_agg_lat"] = stat["avg_agg_lat"][:stat_len]
        stat["p95_agg_lat"] = stat["p95_agg_lat"][:stat_len]
        stat["avg_server_lat"] = stat["avg_server_lat"][:stat_len]
        stat["p95_server_lat"] = stat["p95_server_lat"][:stat_len]
        stat["avg_tries"] = stat["avg_tries"][:stat_len]
        stat["avg_lock_wait_time"] = stat["avg_lock_wait_time"][:stat_len]
        for process_name, process_usage in stat["resource"].items():
            process_usage["cpu"] = process_usage["cpu"][:stat_len]
            process_usage["mem"] = process_usage["mem"][:stat_len]

    client_throughput_lim = np.nanmax(
        [
            client_throughput_lim,
            np.nanmax(
                stat["ClientSuccess"]
                + stat["ClientMiss"]
                + stat["ClientTimeout"]
                + stat["ClientError"]
                + stat["ClientTransactionError"]
            ),
        ]
    )
    client_latency_lim = np.nanmax([client_latency_lim, np.nanmax(stat["p95_agg_lat"])])
    server_throughput_lim = np.nanmax(
        [
            server_throughput_lim,
            np.nanmax(
                stat["ServerSuccess"]
                + stat["ServerMiss"]
                + stat["ServerError"]
                + stat["ServerTimeout"]
                + stat["ServerTransactionError"]
            ),
        ]
    )
    server_latency_lim = np.nanmax(
        [server_latency_lim, np.nanmax(stat["p95_server_lat"])]
    )
    tries_lim = np.nanmax([tries_lim, np.nanmax(stat["avg_tries"])])
    lock_time_lim = np.nanmax([lock_time_lim, np.nanmax(stat["avg_lock_wait_time"])])
    for process_name, process_usage in stat["resource"].items():
        cpu_ylim = np.nanmax([cpu_ylim, np.nanmax(process_usage["cpu"])])
        mem_ylim = np.nanmax([mem_ylim, np.nanmax(process_usage["mem"])])

    print(
        f"{os.path.basename(stat_file)[:-10]}-------------------------------------------"
    )
    if not np.isnan(stat["crash_time"]):
        idx = int(stat["crash_time"]) - 1
        print(
            "before crash avg latency_client_avg", np.nanmean(stat["avg_agg_lat"][:idx])
        )
        print(
            "before crash avg latency_client_p95", np.nanmean(stat["p95_agg_lat"][:idx])
        )
        print(
            "before crash avg latency_server_avg",
            np.nanmean(stat["avg_server_lat"][:idx]),
        )
        print(
            "before crash avg latency_server_p95",
            np.nanmean(stat["p95_server_lat"][:idx]),
        )
    # if not np.isnan(stat["reboot_time"]):
    #     if not np.isnan(stat["replay_time"]):
    #         xlim = stat["replay_time"]
    #     else:
    #         xlim = stat["reboot_time"]
    #     idx = int(xlim) + 1
    #     print(
    #         "after reboot/replay avg latency_client_avg",
    #         np.nanmean(stat["avg_agg_lat"][idx:]),
    #     )
    #     print(
    #         "after reboot/replay avg latency_client_p95",
    #         np.nanmean(stat["p95_agg_lat"][idx:]),
    #     )
    #     print(
    #         "after reboot/replay avg latency_server_avg",
    #         np.nanmean(stat["avg_server_lat"][idx:]),
    #     )
    #     print(
    #         "after reboot/replay avg latency_server_p95",
    #         np.nanmean(stat["p95_server_lat"][idx:]),
    #     )
    #     duration = 30
    #     print(
    #         f"{duration}s after reboot/replay avg client error rate",
    #         np.nanmean(
    #             np.array(stat["ClientError"][idx : idx + duration])
    #             / (
    #                 np.array(stat["ClientSuccess"][idx : idx + duration])
    #                 + np.array(stat["ClientError"][idx : idx + duration])
    #             )
    #         ),
    #     )
    # if not np.isnan(stat["replay_time"]):
    #     crash_time = int(stat["crash_time"]) + 1
    #     reboot_time = int(stat["reboot_time"])
    #     replay_time = int(stat["replay_time"])
    #     server_success = np.array(stat["ServerSuccess"])
    #     server_miss = np.array(stat["ServerMiss"])
    #     hit_rate = server_success / (server_success + server_miss)
    #     print(
    #         f"avg hit rate from crash to replay: {np.nanmean(hit_rate[crash_time:replay_time])}"
    #     )
    #     print(
    #         "admission control rate: ",
    #         stat["ServerError"][reboot_time]
    #         / (stat["ServerSuccess"][reboot_time] + stat["ServerError"][reboot_time]),
    #     )
    #     print(
    #         "admission control rate (+1s): ",
    #         stat["ServerError"][reboot_time + 1]
    #         / (
    #             stat["ServerSuccess"][reboot_time + 1]
    #             + stat["ServerError"][reboot_time + 1]
    #         ),
    #     )
    #     print(
    #         "emergency mode avg latency_server_avg",
    #         np.nanmean(stat["avg_server_lat"][crash_time:replay_time]),
    #     )
    #     print(
    #         "emergency mode avg latency_server_p95",
    #         np.nanmean(stat["p95_server_lat"][crash_time:replay_time]),
    #     )
    #     print(
    #         "emergency mode avg latency_client_avg",
    #         np.nanmean(stat["avg_agg_lat"][crash_time:replay_time]),
    #     )
    #     print(
    #         "emergency mode avg latency_client_p95",
    #         np.nanmean(stat["p95_agg_lat"][crash_time:replay_time]),
    #     )
    stats.append(stat)


def plot(duration, name, filenames):
    # Define plot configurations
    plot_configs = [
        ("throughput_client", plot_throughput, ["Client", client_throughput_lim]),
        (
            "latency_client_avg",
            plot_latency,
            ["avg_agg_lat", "Avg Client Latency", client_latency_lim],
        ),
        (
            "latency_client_p95",
            plot_latency,
            ["p95_agg_lat", "95% Client Latency", client_latency_lim],
        ),
        ("tries", plot_tries, [tries_lim]),
        ("throughput_server", plot_throughput, ["Server", server_throughput_lim]),
        (
            "latency_server_avg",
            plot_latency,
            ["avg_server_lat", "Avg Server Latency", server_latency_lim],
        ),
        (
            "latency_server_p95",
            plot_latency,
            ["p95_server_lat", "95% Server Latency", server_latency_lim],
        ),
        ("resource_cpu", plot_resource, ["cpu", cpu_ylim]),
        ("resource_mem", plot_resource, ["mem", mem_ylim]),
        (
            "lock_wait_time",
            plot_latency,
            ["avg_lock_wait_time", "Avg Lock Wait Time", lock_time_lim],
        ),
    ]

    # Create and save individual plots for each row
    for row, (plot_name, plot_func, args) in enumerate(plot_configs):
        fig, axs = plt.subplots(1, cnt, figsize=(5 * cnt, 4))
        if cnt == 1:
            axs = [axs]

        for i in range(cnt):
            xlim = min(duration, len(stats[i]["cnt"]))
            axs[i].set_title(os.path.basename(filenames[i])[:-10])

            # Call plot function with appropriate arguments
            if plot_func == plot_throughput:
                plot_func(axs[i], stats[i], args[0], args[1] * 1.1, xlim)
            elif plot_func == plot_tries:
                plot_func(axs[i], stats[i], args[0] * 1.1, xlim)
            elif plot_func == plot_resource:
                plot_func(axs[i], stats[i], args[0], args[1] * 1.1, xlim)
            elif plot_func == plot_latency:
                plot_func(axs[i], stats[i], args[0], args[1], args[2] * 1.1, xlim)
            else:
                raise ValueError(f"Unknown plot function: {plot_func}")

        plt.tight_layout()
        fig.savefig(f"{name}_{plot_name}.png", bbox_inches="tight")
        fig.savefig(f"{name}_{plot_name}.pdf", bbox_inches="tight")
        plt.close(fig)


plot(args.total_time, "leveldb" + str(args.total_time), args.filenames)
