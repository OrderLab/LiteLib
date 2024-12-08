#!/usr/bin/env python

import sys
import pandas as pd
import matplotlib.pyplot as plt
import argparse
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('-o', '--output', help="path to output image file")
parser.add_argument('-t', '--start-time', type=int, default=0, help="start time (default: 0)")
parser.add_argument('-c', '--crash-time', type=int, default=-1, help="crash time")
parser.add_argument('input', help="path to input data file")

def plot_throughput(df, start_time, crash_time, output):
    df['timestamp'] = df['timestamp'] - start_time
    df = df[df['timestamp'] >= 0]

    df['normal'] = df['read'] + df['update']
    df['non_success'] = df['read_failed'] + df['update_failed'] + df['read_missed']

    avg_throughput = df['normal'].mean()

    fig, ax = plt.subplots(figsize=(10, 3))

    ax.plot(df['timestamp'], df['normal'], color='green', label='Normal Operations', linewidth=0.5)
    ax.fill_between(df['timestamp'], df['normal'], color='green', alpha=0.5, linewidth=0)

    # ax.plot(df['timestamp'], df['normal'] + df['non_success'], color='red', label='Non-Successful Operations', linewidth=0.5)
    # ax.fill_between(df['timestamp'], df['normal'], df['normal'] + df['non_success'], color='red', alpha=0.5,  linewidth=0)

    if crash_time >= 0:
        ax.axvline(x=crash_time, color='red', linestyle='--', label='Crash Time')

    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Operations')
    ax.set_title('Throughput Over Time')

    # ax.set_xlim(0, df['timestamp'].max())
    ax.set_ylim(0, 150000)

    if output:
        plt.savefig(output)
    else:
        plt.show()
if __name__ == '__main__':
    args = parser.parse_args()
    if not args.input:
        sys.stderr.write('Must specify input data file\n')
        sys.exit(1)
    
    df = pd.read_csv(args.input)
    plot_throughput(df, args.start_time, args.crash_time, args.output)
    
    # python plot.py data/tp.csv -t 0 -c 10 -o tp.png
    # python plot.py data/tp-lite.csv -t 0 -c 10 -o lite.png