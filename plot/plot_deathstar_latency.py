#!/usr/bin/env python3

import argparse

import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


matplotlib.rcParams["pdf.fonttype"] = 42
matplotlib.rcParams["ps.fonttype"] = 42


def parse_log_file(path):
    timestamps = []
    latencies = []
    first_segment_removed = False
    previous_timestamp = None

    with open(path) as source:
        for line in source:
            if not line.startswith("["):
                continue
            try:
                timestamp = float(line.split("[", 1)[1].split("s", 1)[0])
                parts = line.strip().split("]", 1)[1].split(",")
            except (IndexError, ValueError):
                continue

            if not first_segment_removed:
                if (
                    previous_timestamp is not None
                    and timestamp < previous_timestamp
                ):
                    first_segment_removed = True
                else:
                    previous_timestamp = timestamp
                    continue

            latency = np.nan
            for part in parts:
                if "mean=" not in part:
                    continue
                value = part.split("mean=", 1)[1].strip()
                try:
                    latency = (
                        float(value[:-1]) * 1000
                        if value.endswith("s")
                        else float(value)
                    )
                except ValueError:
                    pass
                break
            timestamps.append(timestamp)
            latencies.append(latency)

    return pd.DataFrame({"timestamp": timestamps, "latency": latencies})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("filenames", nargs=2)
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args()

    labels = ["(a) Vanilla", "(b) LiteLib"]
    colors = ["#d95f02", "#1b9e77"]
    figure, axes = plt.subplots(1, 2, figsize=(6.4, 2.4), sharey=True)

    for axis, path, label, color in zip(
        axes, args.filenames, labels, colors
    ):
        data = parse_log_file(path).dropna()
        if data.empty:
            raise ValueError(f"no latency samples in {path}")
        axis.plot(data["timestamp"], data["latency"], color=color, linewidth=1.4)
        axis.axvline(20, color="black", linestyle="--", linewidth=0.9)
        axis.set_title(label)
        axis.set_xlabel("Time (s)")
        axis.grid(axis="y", alpha=0.25)

    axes[0].set_ylabel("Mean latency (ms)")
    figure.tight_layout()
    figure.savefig(args.output, bbox_inches="tight")


if __name__ == "__main__":
    main()
