#!/usr/bin/env python3
import argparse
import glob
import json
import math
import os

import numpy as np


def load(path):
    with open(path) as stream:
        return json.load(stream)


def cv(values):
    values = np.asarray(values, dtype=float)
    mean = np.nanmean(values)
    return float(np.nanstd(values) / mean) if mean else math.inf


def process_memory(row, lite=False):
    matches = []
    for name, info in row.items():
        if name == "time" or not isinstance(info, dict):
            continue
        lower = name.lower()
        if lite:
            if name == "LiteLevelDB":
                matches.append(float(info.get("mem", 0)))
        elif "leveldb" in lower and name != "LiteLevelDB":
            matches.append(float(info.get("mem", 0)))
    return max(matches, default=0.0)


def precrash_memory(path, crash_second, lite=False):
    rows = []
    with open(path) as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(row.get("time"), int) and row["time"] < crash_second:
                rows.append((row["time"], process_memory(row, lite)))
    positive = [value for _, value in rows if value > 0]
    if not positive:
        raise ValueError(f"no memory samples in {path}")
    plateau = max(positive)
    candidates = [
        (second, value)
        for second, value in rows
        if value >= plateau * 0.5
    ]
    if not candidates:
        raise ValueError(f"no stable pre-crash memory sample in {path}")
    return candidates[-1][1]


def normalize_stat(stat, crash_second):
    if math.isnan(stat.get("crash_time", math.nan)):
        stat["crash_time"] = float(crash_second)
    return stat


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--output", required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    os.makedirs(args.output, exist_ok=True)

    metadata = load(os.path.join(args.root, "metadata.json"))
    crash = int(metadata["crash_second"])
    target = float(metadata["rps"])
    stats = {}
    memory = {}
    for mode in ("full", "ebpf", "checkpoint"):
        paths = sorted(
            glob.glob(os.path.join(args.root, f"{mode}*.stat.pruned.json"))
        )
        if not paths:
            raise ValueError(f"no {mode} statistics in {args.root}")
        stat = normalize_stat(load(paths[-1]), crash)
        stats[mode] = stat
        with open(os.path.join(args.output, f"{mode}.stat.json"), "w") as stream:
            json.dump(stat, stream)
        monitor = sorted(
            glob.glob(os.path.join(args.root, f"monitor.{mode}*.jsonl"))
        )[-1]
        memory[mode] = {
            "full": precrash_memory(monitor, crash),
        }
        if mode == "ebpf":
            memory[mode]["lite"] = precrash_memory(monitor, crash, lite=True)

    pre = slice(max(10, crash - 50), crash - 10)
    full_restart = int(stats["full"].get("reboot_time", crash + 1))
    ebpf_replay = int(stats["ebpf"].get("replay_time", crash + 1))
    post_end = min(len(stats["full"]["ServerSuccess"]), full_restart + 50)
    full_pre = np.asarray(stats["full"]["ServerSuccess"][pre], dtype=float)
    full_post = np.asarray(
        stats["full"]["ServerSuccess"][full_restart + 2 : post_end], dtype=float
    )
    ebpf_pre = np.asarray(stats["ebpf"]["ServerSuccess"][pre], dtype=float)
    ebpf_failure = np.asarray(
        stats["ebpf"]["ServerSuccess"][crash : ebpf_replay + 1], dtype=float
    )
    ebpf_post_success = np.asarray(
        stats["ebpf"]["ServerSuccess"][ebpf_replay + 2 : ebpf_replay + 42],
        dtype=float,
    )
    ebpf_failure_error = np.asarray(
        stats["ebpf"]["ServerError"][crash : ebpf_replay + 1], dtype=float
    )
    ebpf_post_miss = np.asarray(
        stats["ebpf"]["ServerMiss"][ebpf_replay + 2 : ebpf_replay + 42],
        dtype=float,
    )
    ebpf_post_timeout = np.asarray(
        stats["ebpf"]["ServerTimeout"][ebpf_replay + 2 : ebpf_replay + 42],
        dtype=float,
    )
    ebpf_post_error = np.asarray(
        stats["ebpf"]["ServerError"][ebpf_replay + 2 : ebpf_replay + 42],
        dtype=float,
    )

    summary = {
        "target_rps": target,
        "full": {
            "pre_mean": float(np.nanmean(full_pre)),
            "pre_cv": cv(full_pre),
            "post_mean": float(np.nanmean(full_post)),
            "post_cv": cv(full_post),
        },
        "ebpf": {
            "pre_mean": float(np.nanmean(ebpf_pre)),
            "failure_success": float(np.nansum(ebpf_failure)),
            "failure_error": float(np.nansum(ebpf_failure_error)),
            "post_success_mean": float(np.nanmean(ebpf_post_success)),
            "post_miss": float(np.nansum(ebpf_post_miss)),
            "post_timeout": float(np.nansum(ebpf_post_timeout)),
            "post_error": float(np.nansum(ebpf_post_error)),
            "post_cv": cv(ebpf_post_success),
        },
        "memory": {
            "full": memory["full"]["full"],
            "lite": {
                "full+": memory["ebpf"]["full"],
                "lite": memory["ebpf"]["lite"],
            },
            "checkpoint": memory["checkpoint"]["full"],
        },
    }
    if args.check:
        if summary["full"]["pre_mean"] < target * 0.85:
            raise ValueError("baseline is overloaded before the crash")
        if summary["full"]["post_cv"] < 0.04:
            raise ValueError("baseline throughput is too stable after restart")
        if summary["ebpf"]["failure_success"] <= 0:
            raise ValueError("LiteLib served no successful requests during failure")
        if summary["ebpf"]["failure_error"] > 0:
            raise ValueError("LiteLib returned stale data during failure")
        if abs(
            summary["ebpf"]["post_success_mean"] - summary["ebpf"]["pre_mean"]
        ) > target * 0.1:
            raise ValueError("LiteLib post-replay throughput differs from pre-crash")
        if any(
            summary["ebpf"][key] > 0
            for key in ("post_miss", "post_timeout", "post_error")
        ):
            raise ValueError("LiteLib returned unsuccessful responses after replay")
        if summary["ebpf"]["post_cv"] > 0.05:
            raise ValueError("LiteLib post-replay throughput is unstable")

    with open(os.path.join(args.output, "summary.json"), "w") as stream:
        json.dump(summary, stream, indent=2)
    with open(os.path.join(args.output, "memory.json"), "w") as stream:
        json.dump({"LevelDB": summary["memory"]}, stream, indent=2)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
