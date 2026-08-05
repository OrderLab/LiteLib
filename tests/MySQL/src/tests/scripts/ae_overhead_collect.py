#!/usr/bin/env python3
import argparse
import glob
import json
import os
import re

import numpy as np


AVG = re.compile(r"latency \(ms\):.*?avg:\s*([0-9.]+)", re.S | re.I)


def latency(path):
    text = open(path, errors="replace").read()
    match = AVG.search(text)
    if not match:
        raise ValueError(f"no final average latency in {path}")
    return float(match.group(1))


def samples(paths, start=10, end=50):
    result = {}
    for path in paths:
        with open(path) as stream:
            for line in stream:
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not start <= row.get("time", -1) < end:
                    continue
                for name, info in row.items():
                    if name != "time":
                        result.setdefault(name, []).append(float(info["cpu"]))
    return result


def mean_for(data, names):
    values = []
    for name, samples_for_name in data.items():
        if any(token.lower() in name.lower() for token in names):
            values.extend(samples_for_name)
    if not values:
        raise ValueError(f"no CPU samples for {names}")
    return float(np.mean(values))


def node_total(paths, names):
    data = samples(paths)
    return sum(mean_for(data, (name,)) for name in names)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--output", required=True)
    parser.add_argument("--expected-repeats", type=int, default=3)
    args = parser.parse_args()

    latency_data = {}
    for mode in ("full", "proxy", "replica", "ndb-client", "ndb-proxy"):
        paths = sorted(glob.glob(os.path.join(args.root, f"sysbench-{mode}-*.log")))
        if len(paths) != args.expected_repeats:
            raise ValueError(
                f"expected {args.expected_repeats} {mode} logs, found {len(paths)}"
            )
        latency_data[mode] = float(np.mean([latency(path) for path in paths]))

    full = glob.glob(os.path.join(args.root, "monitor-node2-full-*.jsonl"))
    proxy = glob.glob(os.path.join(args.root, "monitor-node0-proxy-*.jsonl"))
    replica_primary = glob.glob(
        os.path.join(args.root, "monitor-node2-replica-*.jsonl")
    )
    replica_peer = glob.glob(
        os.path.join(args.root, "monitor-node3-replica-*.jsonl")
    )
    replica_proxy = glob.glob(
        os.path.join(args.root, "monitor-node0-replica-*.jsonl")
    )

    cpu_data = {
        "full": mean_for(samples(full), ("mysqld",)),
        "proxy": {
            "full": mean_for(samples(proxy), ("mysqld",)),
            "lite": mean_for(samples(proxy), ("LiteMySQL",)),
        },
        "replica": {
            "master": mean_for(samples(replica_primary), ("mysqld",)),
            "other": (
                mean_for(samples(replica_peer), ("mysqld",))
                + mean_for(samples(replica_proxy), ("proxysql",))
                + mean_for(samples(replica_proxy), ("orchestrator",))
            ),
        },
    }

    for mode in ("ndb-client", "ndb-proxy"):
        node2 = glob.glob(os.path.join(args.root, f"monitor-node2-{mode}-*.jsonl"))
        node3 = glob.glob(os.path.join(args.root, f"monitor-node3-{mode}-*.jsonl"))
        node0 = glob.glob(os.path.join(args.root, f"monitor-node0-{mode}-*.jsonl"))
        other_names = ("ndb_mgmd",) if mode == "ndb-client" else ("ndb_mgmd", "proxysql")
        cpu_data[mode] = {
            "node1": node_total(node2, ("ndbd", "mysqld")),
            "node2": node_total(node3, ("ndbd", "mysqld")),
            "other": node_total(node0, other_names),
        }

    result = {"latency": latency_data, "cpu": cpu_data}
    os.makedirs(args.output, exist_ok=True)
    with open(os.path.join(args.output, "mysql.json"), "w") as stream:
        json.dump(result, stream, indent=2)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
