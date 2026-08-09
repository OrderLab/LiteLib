#!/usr/bin/env python

import json
import argparse
import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import os
from datetime import datetime
import sys

matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

parser = argparse.ArgumentParser(description="Plot LevelDB throughput comparison data.")

parser.add_argument('-o', '--output',
                    help="path to output image file")
parser.add_argument('-s', "--start_time", type=int, default=0,
                    help="start time")
parser.add_argument("-t", "--total_time", type=int, default=7200,
                    help="maximum duration")
parser.add_argument("filenames", nargs="+",
                    help="The path to the JSON file(s)")

def annotate_time_points(ax, stat, start_time):
    type = [
        ("crash_time", "Crash", "red", (2, 2, 2, 2)),
        ("reboot_time", "Restart Done", "orange", (3, 2, 2, 0)),
        ("replay_time", "Replay Done", "green", (1, 2, 2, 2)),
    ]
    lines = []
    for t, label, c, dash in type:
        if not np.isnan(stat[t]):
            lines.append(ax.axvline(x=stat[t] - start_time, color=c, dashes=dash, label=label, linewidth=2))
    return lines

def plot_single(ax, stat, title, start_time, show_xlabel=True, show_ylabel=True):
    type = [
        ("Success", "tab:green", "//"),
        ("Miss", "tab:orange", "\\\\"),
        ("Timeout", "tab:red", ""),
        ("Error", "tab:purple", "||"),
        ("TransactionError", "0", ""),
    ]

    handles = []
    labels = []

    total_time = len(stat["cnt"])
    base = np.zeros(total_time)
    for t, c, pattern in type:
        next_base = base + stat["Server" + t]
        if (next_base != base).any():
            label = t
            if t == "Error":
                label = (
                    "Stale Data"
                    if np.isnan(stat["replay_time"])
                    else "Admission Control"
                )
            handle = ax.fill_between(
                range(total_time),
                base,
                next_base,
                label=label,
                alpha=0.5,
                color=c,
                hatch=pattern,
                edgecolor='black',
                linewidth=0.5
            )
            handles.append(handle)
            labels.append(label)
            base = next_base

    lines = annotate_time_points(ax, stat, start_time)
    for line in lines:
        handles.append(line)
        labels.append(line.get_label())

    if show_xlabel:
        ax.set_xlabel('Time (s)', fontsize=11)
    else:
        ax.set_xticklabels([])
    if show_ylabel:
        ax.set_ylabel('Throughput (rps)', fontsize=11)
    else:
        ax.set_ylabel('')

    # Set y-axis limits
    max_throughput = np.nanmax(
        stat["ServerSuccess"]
        + stat["ServerMiss"]
        + stat["ServerError"]
        + stat["ServerTimeout"]
        + stat["ServerTransactionError"]
    )
    ax.set_ylim(0, max_throughput * 1.1)

    if title == '(c) Checkpoint (every 30s)':
        ax.set_title(title, y=-0.4, x=0, ha="left")
    else:
        ax.set_title(title, y=-0.25, x=0, ha="left")

    if title == '(a) Vanilla':
        # Create dummy handles for all possible categories
        all_types = [
            ("Success", "tab:green", "//"),
            ("Miss", "tab:orange", "\\\\"),
            ("Timeout", "tab:red", ""),
            ("Stale Data", "tab:purple", "||"),
            ("Crash", "red"),
            ("Restart Done", "orange"),
            ("Replay Done", "green"),
        ]
        dummy_handles = []
        dummy_labels = []
        # First add the filled areas
        for t, c, pattern in all_types[:4]:
            dummy_handles.append(matplotlib.patches.Patch(facecolor=c, alpha=0.5, hatch=pattern, edgecolor='black', linewidth=0.5))
            dummy_labels.append(t)
        # Then add the vertical lines
        for t, c in all_types[4:]:
            dummy_handles.append(matplotlib.lines.Line2D([], [], color=c, dashes=(2, 2, 2, 2), linewidth=2))
            dummy_labels.append(t)
        
        # Reorder the handles and labels to match the desired order
        # Current order: [Success, Miss, Timeout, Stale Data, Crash, Restart Done, Replay Done]
        order = [0, 4, 1, 5, 2, 6, 3]  # Map from current display to desired order
        dummy_handles = [dummy_handles[i] for i in order]
        dummy_labels = [dummy_labels[i] for i in order]
        
        ax.legend(dummy_handles, dummy_labels, loc='lower left', bbox_to_anchor=(0., 0.92), frameon=False, fontsize=11, ncol=4, columnspacing=0.5, handletextpad=0.5)

    return ax

def process_stats(args):
    stats = []
    for i in range(len(args.filenames)):
        stat_file = args.filenames[i]
        with open(stat_file, "r") as f:
            stat = json.load(f)

        stat_len = len(stat["cnt"])
        if stat_len < args.total_time:
            args.total_time = stat_len
        stat_len = args.total_time - args.start_time
        stat["cnt"] = stat["cnt"][args.start_time:args.start_time + stat_len]
        stat["ServerSuccess"] = stat["ServerSuccess"][args.start_time:args.start_time + stat_len]
        stat["ServerMiss"] = stat["ServerMiss"][args.start_time:args.start_time + stat_len]
        stat["ServerTimeout"] = stat["ServerTimeout"][args.start_time:args.start_time + stat_len]
        stat["ServerError"] = stat["ServerError"][args.start_time:args.start_time + stat_len]
        stat["ServerTransactionError"] = stat["ServerTransactionError"][args.start_time:args.start_time + stat_len]

        stats.append(stat)
    return stats

def plot(duration, stats, output, start_time):
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(8, 5))  # 3 rows, 1 column
    
    titles = ['(a) Vanilla', '(b) LiteLib', '(c) Checkpoint (every 30s)']
    axes = [ax1, ax2, ax3]
    
    for idx, (stat, ax, title) in enumerate(zip(stats, axes, titles)):
        plot_single(ax, stat, title, args.start_time,
                   show_xlabel = idx == len(axes) - 1,
                   show_ylabel = idx == 1)  # Only show ylabel for LiteSys (index 1)
    
    fig.tight_layout()
    fig.subplots_adjust(hspace=0.3)
    if output:
        plt.savefig(output, bbox_inches='tight')
    else:
        plt.show()

if __name__ == '__main__':
    args = parser.parse_args()
    if len(args.filenames) != 3:
        raise argparse.ArgumentTypeError("Invalid number of files. Expected 3 (vanilla, lite, checkpoint).")
    for filename in args.filenames:
        if not filename.endswith(".json"):
            raise argparse.ArgumentTypeError(
                f"Invalid file type: {filename}. Expected a '.json' file."
            )
    stats = process_stats(args)
    plot(args.total_time, stats, args.output, args.start_time)
