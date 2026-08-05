#!/usr/bin/env python3
import argparse
import glob
import json
import os

import numpy as np


def load(path):
    with open(path) as stream:
        return json.load(stream)


def row_at(path, second):
    with open(path) as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if row.get("time") == second:
                return row
    raise ValueError(f"no sample at second {second} in {path}")


def entries(row, token):
    return [
        info
        for name, info in row.items()
        if name != "time" and token.lower() in name.lower()
    ]


def mean(values):
    return float(np.mean(values))


def redis_memory(root, second):
    full = []
    embedded_full = []
    embedded_lite = []
    replica_master = []
    replica_other = []
    for rep in (1, 2, 3):
        vanilla = row_at(
            os.path.join(root, f"monitor-node3-vanilla-{rep}.log"), second
        )
        full.append(entries(vanilla, "redis-server-vanilla")[0]["mem"])

        embedded = row_at(
            os.path.join(root, f"monitor-node3-embedded-{rep}.log"), second
        )
        full_info = entries(embedded, "redis-server")[0]
        lite_info = entries(embedded, "redis-lite")[0]
        shared = max(
            float(full_info.get("shared_mem", 0)),
            float(lite_info.get("shared_mem", 0)),
        )
        embedded_full.append(max(0.0, float(full_info["mem"]) - 2 * shared))
        embedded_lite.append(float(lite_info["mem"]) + shared)

        master = row_at(
            os.path.join(root, f"monitor-node3-replica-{rep}.log"), second
        )
        peer = row_at(
            os.path.join(root, f"monitor-node1-replica-{rep}.log"), second
        )
        sentinel = row_at(
            os.path.join(root, f"monitor-node2-replica-{rep}.log"), second
        )
        replica_master.append(entries(master, "redis-server-vanilla")[0]["mem"])
        other = entries(peer, "redis-server-vanilla")[0]["mem"]
        other += sum(info["mem"] for info in entries(master, "sentinel"))
        other += sum(info["mem"] for info in entries(peer, "sentinel"))
        other += sum(info["mem"] for info in entries(sentinel, "sentinel"))
        replica_other.append(other)
    return {
        "full": mean(full),
        "lite": {
            "full+": mean(embedded_full),
            "lite": mean(embedded_lite),
        },
        "replica": {
            "master": mean(replica_master),
            "other": mean(replica_other),
        },
    }


def process_sum(row, tokens):
    total = 0.0
    for name, info in row.items():
        if name == "time":
            continue
        if any(token.lower() in name.lower() for token in tokens):
            total += float(info["mem"])
    return total


def mysql_memory(root, second):
    result = {
        "full": [],
        "lite_full": [],
        "lite_lite": [],
        "replica_master": [],
        "replica_other": [],
        "ndb_client_node1": [],
        "ndb_client_node2": [],
        "ndb_client_other": [],
        "ndb_proxy_node1": [],
        "ndb_proxy_node2": [],
        "ndb_proxy_other": [],
    }
    for rep in (1, 2, 3):
        result["full"].append(
            process_sum(
                row_at(
                    os.path.join(root, f"monitor-node2-full-{rep}.jsonl"),
                    second,
                ),
                ("mysqld",),
            )
        )
        proxy = row_at(
            os.path.join(root, f"monitor-node0-proxy-{rep}.jsonl"), second
        )
        result["lite_full"].append(process_sum(proxy, ("mysqld",)))
        result["lite_lite"].append(process_sum(proxy, ("LiteMySQL",)))

        result["replica_master"].append(
            process_sum(
                row_at(
                    os.path.join(root, f"monitor-node2-replica-{rep}.jsonl"),
                    second,
                ),
                ("mysqld",),
            )
        )
        replica_other = process_sum(
            row_at(
                os.path.join(root, f"monitor-node3-replica-{rep}.jsonl"),
                second,
            ),
            ("mysqld",),
        )
        replica_other += process_sum(
            row_at(
                os.path.join(root, f"monitor-node0-replica-{rep}.jsonl"),
                second,
            ),
            ("proxysql", "orchestrator"),
        )
        result["replica_other"].append(replica_other)

        for mode, key in (("ndb-client", "ndb_client"), ("ndb-proxy", "ndb_proxy")):
            result[f"{key}_node1"].append(
                process_sum(
                    row_at(
                        os.path.join(root, f"monitor-node2-{mode}-{rep}.jsonl"),
                        second,
                    ),
                    ("ndbd", "mysqld"),
                )
            )
            result[f"{key}_node2"].append(
                process_sum(
                    row_at(
                        os.path.join(root, f"monitor-node3-{mode}-{rep}.jsonl"),
                        second,
                    ),
                    ("ndbd", "mysqld"),
                )
            )
            result[f"{key}_other"].append(
                process_sum(
                    row_at(
                        os.path.join(root, f"monitor-node0-{mode}-{rep}.jsonl"),
                        second,
                    ),
                    ("ndb_mgmd", "proxysql")
                    if mode == "ndb-proxy"
                    else ("ndb_mgmd",),
                )
            )
    return {
        "full": mean(result["full"]),
        "lite": {
            "full": mean(result["lite_full"]),
            "lite": mean(result["lite_lite"]),
        },
        "replica": {
            "master": mean(result["replica_master"]),
            "other": mean(result["replica_other"]),
        },
        "ndb(client)": {
            "node1": mean(result["ndb_client_node1"]),
            "node2": mean(result["ndb_client_node2"]),
            "other": mean(result["ndb_client_other"]),
        },
        "ndb(proxy)": {
            "node1": mean(result["ndb_proxy_node1"]),
            "node2": mean(result["ndb_proxy_node2"]),
            "other": mean(result["ndb_proxy_other"]),
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True)
    parser.add_argument("--leveldb", required=True)
    parser.add_argument("--redis", required=True)
    parser.add_argument("--mysql", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--redis-second", type=int, default=124)
    parser.add_argument("--mysql-second", type=int, default=59)
    args = parser.parse_args()

    data = load(args.base)
    data.update(load(args.leveldb))
    data["Redis"] = redis_memory(args.redis, args.redis_second)
    data["MySQL"] = mysql_memory(args.mysql, args.mysql_second)
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w") as stream:
        json.dump(data, stream, indent=2)
    print(json.dumps(data, indent=2))


if __name__ == "__main__":
    main()
