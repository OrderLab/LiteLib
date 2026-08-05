#!/usr/bin/env python3
import argparse
import glob
import json
import os
import re

import numpy as np


SUMMARY = re.compile(r"^\[(READ|UPDATE)\], AverageLatency\(us\), ([0-9.]+)")


def latency(path):
    values = {}
    with open(path, errors="replace") as stream:
        for line in stream:
            match = SUMMARY.match(line)
            if match:
                values[match.group(1)] = float(match.group(2))
    if set(values) != {"READ", "UPDATE"}:
        raise ValueError(f"missing YCSB latency summary in {path}")
    return (0.8 * values["READ"] + 0.2 * values["UPDATE"]) / 1000.0


def process_samples(path, start=90, end=180):
    samples = {}
    with open(path) as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not start <= row.get("time", -1) <= end:
                continue
            for name, info in row.items():
                if name != "time":
                    samples.setdefault(name, []).append(float(info["cpu"]))
    return samples


def matching_mean(paths, prefix, start, end):
    values = []
    for path in paths:
        for name, samples in process_samples(path, start, end).items():
            if name.startswith(prefix):
                values.extend(samples)
    if not values:
        raise ValueError(f"no {prefix} CPU samples in {paths}")
    return float(np.mean(values))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--output", required=True)
    parser.add_argument("--start", type=int, default=90)
    parser.add_argument("--end", type=int, default=180)
    args = parser.parse_args()

    latency_data = {}
    for mode in ("vanilla", "embedded", "replica"):
        paths = sorted(glob.glob(os.path.join(args.root, f"benchmark-{mode}-*.log")))
        if not paths:
            raise ValueError(f"no {mode} benchmark logs")
        latency_data[mode] = float(np.mean([latency(path) for path in paths]))

    vanilla_monitors = glob.glob(
        os.path.join(args.root, "monitor-node3-vanilla-*.log")
    )
    embedded_monitors = glob.glob(
        os.path.join(args.root, "monitor-node3-embedded-*.log")
    )
    replica_master = glob.glob(
        os.path.join(args.root, "monitor-node3-replica-*.log")
    )
    replica_peer = glob.glob(
        os.path.join(args.root, "monitor-node1-replica-*.log")
    )
    replica_all = glob.glob(
        os.path.join(args.root, "monitor-node*-replica-*.log")
    )

    cpu_data = {
        "vanilla": matching_mean(
            vanilla_monitors, "redis-server-vanilla", args.start, args.end
        ),
        "embedded": matching_mean(
            embedded_monitors, "redis-server", args.start, args.end
        ),
        "replica": {
            "master": matching_mean(
                replica_master, "redis-server-vanilla", args.start, args.end
            ),
            "other": (
                matching_mean(
                    replica_peer, "redis-server-vanilla", args.start, args.end
                )
                + matching_mean(
                    replica_all, "redis-sentinel-vanilla", args.start, args.end
                )
            ),
        },
    }

    result = {"latency": latency_data, "cpu": cpu_data}
    os.makedirs(args.output, exist_ok=True)
    with open(os.path.join(args.output, "redis.json"), "w") as stream:
        json.dump(result, stream, indent=2)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
