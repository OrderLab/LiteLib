#!/usr/bin/env python3

import argparse
import math

import matplotlib
import matplotlib.pyplot as plt
import numpy as np


matplotlib.rcParams["pdf.fonttype"] = 42
matplotlib.rcParams["ps.fonttype"] = 42


def process(path, total_time):
    hits = np.zeros(total_time + 1)
    with open(path) as source:
        start = int(source.readline().strip())
        for line in source:
            fields = line.split()
            if len(fields) < 4:
                continue
            request_start, duration, cache_hit, _ = map(int, fields[:4])
            second = math.ceil(
                (request_start + duration - start) / 1_000_000_000
            )
            if 0 <= second <= total_time:
                hits[second] += cache_hit
    return hits


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("filenames", nargs=3)
    parser.add_argument("-r", "--arrival-rate", type=float, required=True)
    parser.add_argument("-t", "--time", type=int, required=True)
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args()

    labels = ["Full", "LiteLib", "Checkpoint"]
    colors = ["#d95f02", "#1b9e77", "#7570b3"]
    series = [process(path, 300) for path in args.filenames]
    crash = int(np.argmin(series[0][10:]) + 10)

    figure, axis = plt.subplots(figsize=(5.2, 2.7))
    seconds = np.arange(args.time + 1)
    for values, label, color in zip(series, labels, colors):
        axis.plot(
            seconds,
            values[: args.time + 1],
            label=label,
            color=color,
            linewidth=1.3,
        )

    axis.axvline(crash, color="black", linestyle="--", linewidth=0.9)
    axis.axhline(
        args.arrival_rate,
        color="#666666",
        linestyle=":",
        linewidth=0.8,
    )
    axis.set_xlim(0, args.time)
    axis.set_ylim(bottom=0)
    axis.set_xlabel("Time (s)")
    axis.set_ylabel("Cache-hit throughput (requests/s)")
    axis.grid(axis="y", alpha=0.25)
    axis.legend(fontsize=8, ncol=3, loc="upper center")
    figure.tight_layout()
    figure.savefig(args.output, bbox_inches="tight")


if __name__ == "__main__":
    main()
