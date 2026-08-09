#!/usr/bin/env python

import sys
import pandas as pd
import matplotlib.pyplot as plt
import argparse
import numpy as np
import matplotlib
matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

parser = argparse.ArgumentParser()
parser.add_argument('-o', '--output', help="path to output image file")
parser.add_argument('inputs', nargs=2, help="paths to two input log files")

def parse_log_file(file_path):
    throughput = []
    latency = []
    timestamp = []
    errors = []
    
    # Track if we've seen the first increasing segment
    first_segment_removed = False
    prev_ts = None

    with open(file_path, 'r') as f:
        lines = f.readlines()
        for line in lines:
            if line.startswith('['):
                # Parse timestamp, throughput and latency
                parts = line.strip().split(']')[1].split(',')
                ts = float(line.split('[')[1].split('s')[0])

                # Skip the first increasing segment (warmup)
                if not first_segment_removed:
                    if prev_ts is not None and ts < prev_ts:
                        first_segment_removed = True
                    else:
                        prev_ts = ts
                        continue

                # Parse throughput and errors
                tput_part = parts[0].split(':')[1].strip()
                tput = float(tput_part.split()[0])

                # Parse errors if present
                error_count = 0
                if len(parts) > 1 and "Errors:" in parts[1]:
                    error_count = float(parts[1].split(':')[1].split()[0])

                # Parse latency if present
                lat = np.nan
                for part in parts:
                    if "mean=" in part:
                        try:
                            lat_str = part.split('mean=')[1].split(',')[0].strip()
                            if lat_str.endswith('s'):  # Convert seconds to milliseconds
                                lat = float(lat_str[:-1]) * 1000
                            else:  # Already in milliseconds
                                lat = float(lat_str)
                        except:
                            lat = np.nan
                        break

                timestamp.append(ts)
                throughput.append(tput)
                latency.append(lat)
                errors.append(error_count)

    return pd.DataFrame({
        'timestamp': timestamp,
        'throughput': throughput,
        'latency': latency,
        'errors': errors
    })

def plot_throughput_latency(dfs, output):
    fig, axs = plt.subplots(2, 1, figsize=(8, 4), sharex=True, gridspec_kw={'hspace':0.3})

    titles = ['(a) Vanilla', '(b) LiteLib']
    duration = 180
    crash_time = 20  # Crash time in seconds

    colors = {
        'latency': 'tab:blue'  # Blue color for latency line
    }

    # Find the maximum latency across all dataframes
    max_lat = max(df['latency'].max() for df in dfs if not df['latency'].isna().all())

    for i, df in enumerate(dfs):
        # Plot latency
        valid_latency = df[df['latency'].notna()]
        ln2 = axs[i].plot(valid_latency['timestamp'], valid_latency['latency'],
                      color=colors['latency'], label='Mean Latency')
        axs[i].tick_params(axis='y', labelcolor='black')  # Black color for y-axis ticks

        # Add crash time marker
        axs[i].axvline(x=crash_time, color='red', dashes=(2, 2, 2, 2), label='Failure', linewidth=2)

        # Set the same y-limits for both subplots
        axs[i].set_ylim(0, min(max_lat * 1.1, 70000))  # Cap at 70s

        # Add title
        if i == 0:
            axs[i].set_title(titles[i], y=-0.25, x=0, ha="left")
        else:
            axs[i].set_title(titles[i], y=-0.35, x=0, ha="left")

        # Add legend
        if i == 0:
            handles = [ln2[0], axs[i].get_lines()[-1]]  # Get both the latency line and crash line
            labels = ['Mean Latency', 'Failure']
            axs[i].legend(handles, labels, loc='lower left', bbox_to_anchor=(0., 0.92), frameon=False, fontsize=11, ncol=3, columnspacing=1, handletextpad=0.3)

    # Set common x label
    axs[1].set_xlabel('Time (s)', fontsize=11)

    # Add centered y-axis label
    fig.text(0.04, 0.5, 'Average Client Latency (ms)', va='center', rotation='vertical', fontsize=11)

    axs[0].margins(0,0)
    axs[1].margins(0,0)
    plt.tight_layout()

    if output:
        plt.savefig(output, bbox_inches="tight")
    else:
        plt.show()

if __name__ == '__main__':
    args = parser.parse_args()

    # Parse the log files
    dfs = [parse_log_file(input_file) for input_file in args.inputs]
    plot_throughput_latency(dfs, args.output)