#!/usr/bin/env python3

import argparse
import json
import math

import matplotlib
import matplotlib.pyplot as plt
import numpy as np


matplotlib.rcParams["pdf.fonttype"] = 42
matplotlib.rcParams["ps.fonttype"] = 42


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("filenames", nargs=3)
    parser.add_argument("-t", "--time", type=int, default=120)
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args()

    labels = ["(a) Full", "(b) LiteLib", "(c) Checkpoint"]
    colors = ["#d95f02", "#1b9e77", "#7570b3"]
    figure, axes = plt.subplots(1, 3, figsize=(7.2, 2.4), sharey=True)

    for axis, path, label, color in zip(
        axes, args.filenames, labels, colors
    ):
        with open(path) as source:
            data = json.load(source)
        success = np.asarray(data["ClientSuccess"], dtype=float)
        limit = min(args.time, len(success))
        seconds = np.arange(limit)
        axis.plot(seconds, success[:limit], color=color, linewidth=1.2)

        crash = data.get("crash_time")
        if not isinstance(crash, (int, float)) or math.isnan(crash):
            crash = 80
        if crash < limit:
            axis.axvline(crash, color="black", linestyle="--", linewidth=0.9)

        replay = data.get("replay_time")
        if (
            isinstance(replay, (int, float))
            and not math.isnan(replay)
            and replay < limit
        ):
            axis.axvline(replay, color="#666666", linestyle=":", linewidth=0.9)

        axis.set_title(label)
        axis.set_xlabel("Time (s)")
        axis.set_xlim(0, max(limit - 1, 1))
        axis.grid(axis="y", alpha=0.25)

    axes[0].set_ylabel("Successful requests/s")
    figure.tight_layout()
    figure.savefig(args.output, bbox_inches="tight")


if __name__ == "__main__":
    main()
