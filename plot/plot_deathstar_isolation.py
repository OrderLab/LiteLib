#!/usr/bin/env python3

import argparse
import json

import matplotlib
import matplotlib.pyplot as plt
import numpy as np


matplotlib.rcParams["pdf.fonttype"] = 42
matplotlib.rcParams["ps.fonttype"] = 42


def average(entries, section, field):
    values = [entry[section][field] for entry in entries]
    if not values:
        raise ValueError(f"no values for {section}.{field}")
    return float(np.mean(values))


def system_values(data, prefix):
    crash = [
        entry
        for entry in data.get("crash", [])
        if entry["log_file"].startswith(prefix)
    ]
    baseline = [
        entry
        for entry in data.get("nocrash", [])
        if entry["log_file"].startswith(prefix)
    ]
    if not baseline:
        baseline = crash
    if not crash:
        raise ValueError(f"no {prefix} entries in stats")

    before_load = average(baseline, "before_crash", "requests_sum_diff")
    after_load = average(crash, "after_crash", "requests_sum_diff")
    before_failover = average(baseline, "before_crash", "failover_all")
    after_failover = average(crash, "after_crash", "failover_all")
    before_latency = average(baseline, "before_crash", "duration_avg")
    after_latency = average(crash, "after_crash", "duration_avg")

    return {
        "load": [before_load, after_load],
        "latency": [before_latency, after_latency],
        "throughput": [
            before_load - before_failover,
            after_load - after_failover,
        ],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("filenames", nargs="+")
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args()

    with open(args.filenames[0]) as source:
        data = json.load(source)

    systems = [
        ("Vanilla", system_values(data, "vanilla")),
        ("LiteLib", system_values(data, "litesys")),
    ]
    metrics = [
        ("load", "Healthy-instance load"),
        ("latency", "Healthy-instance latency"),
        ("throughput", "Healthy-instance throughput"),
    ]

    figure, axes = plt.subplots(1, 3, figsize=(7.2, 2.5))
    x = np.arange(len(systems))
    width = 0.36
    for axis, (metric, title) in zip(axes, metrics):
        before = [values[metric][0] for _, values in systems]
        after = [values[metric][1] for _, values in systems]
        axis.bar(x - width / 2, before, width, label="Before failure")
        axis.bar(x + width / 2, after, width, label="After failure")
        axis.set_xticks(x, [name for name, _ in systems])
        axis.set_title(title, fontsize=9)
        axis.grid(axis="y", alpha=0.25)

    axes[0].legend(fontsize=7)
    figure.tight_layout()
    figure.savefig(args.output, bbox_inches="tight")


if __name__ == "__main__":
    main()
