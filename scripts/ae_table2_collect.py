#!/usr/bin/env python3
import argparse
import csv
import os
from collections import defaultdict

import numpy as np


ORDER = [
    ("Redis", "active-passive", "30s detection"),
    ("Redis", "active-passive", "5s detection"),
    ("Redis", "LiteLib", "proxy"),
    ("Redis", "LiteLib", "embedded"),
    ("MySQL", "active-passive", "binlog replication"),
    ("MySQL", "active-active", "NDB proxy failover"),
    ("MySQL", "active-active", "NDB client failover"),
    ("MySQL", "LiteLib", "proxy"),
]


def read_rows(path):
    with open(path, newline="") as stream:
        return list(csv.DictReader(stream))


def display(milliseconds):
    if milliseconds >= 1000:
        return f"{milliseconds / 1000:.2f} s"
    return f"{milliseconds:.2f} ms"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    grouped = defaultdict(list)
    for path in args.inputs:
        for row in read_rows(path):
            key = (row["application"], row["solution"], row["setting"])
            grouped[key].append(float(row["gap_ms"]))

    output = []
    for application, solution, setting in ORDER:
        values = grouped[(application, solution, setting)]
        if not values:
            raise ValueError(f"missing Table 2 row: {(application, solution, setting)}")
        mean = float(np.mean(values))
        output.append({
            "application": application,
            "solution": solution,
            "setting": setting,
            "gap_ms": f"{mean:.6f}",
            "gap_display": display(mean),
            "std_ms": f"{float(np.std(values)):.6f}",
            "repetitions": len(values),
        })

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=output[0].keys())
        writer.writeheader()
        writer.writerows(output)
    for row in output:
        print(row)


if __name__ == "__main__":
    main()
