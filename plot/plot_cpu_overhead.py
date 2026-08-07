#!/usr/bin/env python3

import argparse
import json

import matplotlib
import matplotlib.pyplot as plt


matplotlib.rcParams["pdf.fonttype"] = 42
matplotlib.rcParams["ps.fonttype"] = 42


def total(value):
    if isinstance(value, dict):
        return sum(total(component) for component in value.values())
    return float(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args()

    with open(args.input) as source:
        data = json.load(source)

    figure, axes = plt.subplots(
        1, len(data), figsize=(2.25 * len(data), 2.6), sharey=False
    )
    if len(data) == 1:
        axes = [axes]

    for axis, (application, modes) in zip(axes, data.items()):
        baseline = total(modes["full"])
        names = list(modes)
        values = [total(modes[name]) / baseline for name in names]
        bars = axis.bar(range(len(names)), values, color="#f58518")
        axis.axhline(1, color="black", linewidth=0.8)
        axis.set_xticks(range(len(names)), names, rotation=35, ha="right")
        axis.set_title(application)
        axis.set_ylabel("CPU / baseline")
        axis.grid(axis="y", alpha=0.25)
        axis.bar_label(bars, fmt="%.2fx", fontsize=7, padding=2)

    figure.tight_layout()
    figure.savefig(args.output, bbox_inches="tight")


if __name__ == "__main__":
    main()
