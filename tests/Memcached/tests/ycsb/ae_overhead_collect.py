#!/usr/bin/env python3
"""Collect Memcached's Figure 14/15/16 inputs.

Figure 14 uses the exact monitor sample immediately before the full Memcached
process drops at crash time. Figures 15/16 are non-crash overheads; discard the
first 19 seconds and average seconds 20..60 across all repetitions.
"""

import argparse
import glob
import json
import os
import re
import shutil

import numpy as np


STATUS = re.compile(
    r" (\d+) sec:.*?"
    r"\[READ: Count=(\d+).*?Avg=([0-9.]+).*?\].*?"
    r"\[UPDATE: Count=(\d+).*?Avg=([0-9.]+)"
)


def json_rows(path):
    rows = []
    with open(path) as f:
        for line in f:
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return rows


def precrash_sample(path):
    """Return the row immediately before full Memcached loses its RSS."""
    rows = json_rows(path)
    def normalize(row):
        full = [
            info
            for name, info in row.items()
            if name.lower().startswith("memcached")
            and isinstance(info, dict)
            and info.get("mem", 0) > 0
        ]
        normalized = dict(row)
        if full:
            normalized["memcached"] = max(full, key=lambda info: info["mem"])
        return normalized

    rows = [normalize(row) for row in rows]
    samples = [
        (i, r, r.get("memcached", {}).get("mem"))
        for i, r in enumerate(rows)
        if isinstance(r.get("time"), int)
    ]
    positive = [mem for _, _, mem in samples if mem and mem > 0]
    if not positive:
        raise ValueError(f"no Memcached RSS samples in {path}")
    plateau = max(positive)
    for pos, (idx, row, mem) in enumerate(samples):
        if row["time"] < 5:
            continue
        if mem is None or mem < plateau * 0.5:
            if pos == 0:
                break
            return samples[pos - 1][1]
    raise ValueError(f"no Memcached crash/drop found in {path}")


def resolve_overlay(root, token, kind="monitor"):
    for version in ("v2", "v1", "v0"):
        matches = sorted(
            glob.glob(
                os.path.join(root, version, "**", f"*{kind}_*EXP_{token}.txt"),
                recursive=True,
            )
        )
        if matches:
            return matches[-1]
    raise FileNotFoundError(f"no overlay file for {kind}/{token}")


def memory_values(
    deathstar_root,
    metastability_root,
    deathstar_results=None,
    fig13_results=None,
):
    if deathstar_results:
        with open(os.path.join(deathstar_results, "selected", "runs.json")) as f:
            selected = json.load(f)
        d_full = os.path.join(
            deathstar_results,
            "crash",
            os.path.basename(selected["vanilla"]).removesuffix(".log")
            + ".memcached.monitor.1.log",
        )
        d_lite = os.path.join(
            deathstar_results,
            "crash",
            os.path.basename(selected["litesys"]).removesuffix(".log")
            + ".memcached.monitor.1.log",
        )
    else:
        d_full = os.path.join(
            deathstar_root,
            "crash",
            "vanilla_20250409_165350.memcached.monitor.1.log",
        )
        d_lite = os.path.join(
            deathstar_root,
            "crash",
            "litesys_20250409_164739.memcached.monitor.1.log",
        )

    if fig13_results:
        m_full = os.path.join(fig13_results, "monitor_full.log")
        m_lite = os.path.join(fig13_results, "monitor_lite.log")
        m_checkpoint = os.path.join(fig13_results, "monitor_checkpoint.log")
    else:
        m_full = resolve_overlay(metastability_root, "full")
        m_lite = resolve_overlay(metastability_root, "lite")
        m_checkpoint = resolve_overlay(metastability_root, "checkpoint")

    df = precrash_sample(d_full)
    dl = precrash_sample(d_lite)
    mf = precrash_sample(m_full)
    ml = precrash_sample(m_lite)
    mc = precrash_sample(m_checkpoint)
    return {
        "MC(M)": {
            "full": mf["memcached"]["mem"],
            "lite": {
                "full": ml["memcached"]["mem"],
                "lite": ml["LiteMemcached"]["mem"],
            },
            "checkpoint": mc["memcached"]["mem"],
        },
        "MC(D)": {
            "full": df["memcached"]["mem"],
            "lite": {
                "full+": dl["memcached"]["mem"],
                "lite": dl["LiteMemcached"]["mem"],
            },
        },
    }


def final_ycsb_section(path):
    sections = []
    current = []
    with open(path, errors="replace") as f:
        for line in f:
            if re.search(r" 0 sec: 0 operations", line):
                if current:
                    sections.append(current)
                current = []
            match = STATUS.search(line)
            if match:
                current.append(tuple(map(float, match.groups())))
    if current:
        sections.append(current)
    if not sections:
        raise ValueError(f"no YCSB status samples in {path}")
    return sections[-1]


def steady_latency(path, start=20, end=60):
    rows = [row for row in final_ycsb_section(path) if start <= row[0] <= end]
    if not rows:
        raise ValueError(f"no YCSB samples in [{start},{end}] for {path}")
    count = sum(read_n + update_n for _, read_n, _, update_n, _ in rows)
    total = sum(
        read_n * read_avg + update_n * update_avg
        for _, read_n, read_avg, update_n, update_avg in rows
    )
    return total / count


def remove_outliers(values):
    values = np.asarray(values)
    q1, q3 = np.percentile(values, [25, 75])
    iqr = q3 - q1
    return values[(values >= q1 - 1.5 * iqr) & (values <= q3 + 1.5 * iqr)]


def cpu_samples(path, component, start=20, end=60):
    values = []
    for row in json_rows(path):
        time = row.get("time")
        if isinstance(time, int) and start <= time <= end and component in row:
            values.append(float(row[component]["cpu"]))
    return values


def ycsb_values(root):
    latency = {}
    for mode in ("vanilla", "embedded", "proxy"):
        files = glob.glob(os.path.join(root, f"benchmark-{mode}_*.log"))
        latency[mode] = float(np.mean([steady_latency(path) for path in files]))

    components = {
        "vanilla": ("memcached-vanilla",),
        "embedded": ("memcached",),
        "proxy": ("memcached-vanilla", "LiteMemcached"),
    }
    cpu = {}
    for mode, names in components.items():
        files = glob.glob(os.path.join(root, f"monitor-{mode}_*.log"))
        cpu[mode] = {}
        for name in names:
            values = []
            for path in files:
                values.extend(cpu_samples(path, name))
            cpu[mode][name] = float(np.mean(remove_outliers(values)))
    return latency, cpu


def update_json(source, output, updates):
    with open(source) as f:
        data = json.load(f)
    data.update(updates)
    with open(output, "w") as f:
        json.dump(data, f, indent=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw-root", default=os.path.expanduser("~/OriginalRawData"))
    parser.add_argument("--paper-root", default=os.path.expanduser("~/litesys-nsdi27"))
    parser.add_argument(
        "--ycsb-dir",
        help="live YCSB log directory (defaults to OriginalRawData/Memcached/ycsb)",
    )
    parser.add_argument("--deathstar-results")
    parser.add_argument("--fig13-results")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    os.makedirs(args.output, exist_ok=True)

    memory = memory_values(
        os.path.join(args.raw_root, "DeathStar", "20250409_data_motivation"),
        os.path.join(args.raw_root, "Memcached"),
        args.deathstar_results,
        args.fig13_results,
    )
    latency, cpu = ycsb_values(
        args.ycsb_dir or os.path.join(args.raw_root, "Memcached", "ycsb")
    )

    update_json(
        os.path.join(args.paper_root, "data", "overhead", "memory.json"),
        os.path.join(args.output, "memory.json"),
        memory,
    )
    update_json(
        os.path.join(args.paper_root, "data", "overhead", "latency.json"),
        os.path.join(args.output, "latency.json"),
        {
            "Memcached": {
                "full": latency["vanilla"] / 1000.0,
                "embedded": latency["embedded"] / 1000.0,
                "proxy": latency["proxy"] / 1000.0,
            }
        },
    )
    update_json(
        os.path.join(args.paper_root, "data", "overhead", "cpu.json"),
        os.path.join(args.output, "cpu.json"),
        {
            "Memcached": {
                "full": cpu["vanilla"]["memcached-vanilla"],
                "embedded": cpu["embedded"]["memcached"],
                "proxy": {
                    "full": cpu["proxy"]["memcached-vanilla"],
                    "lite": cpu["proxy"]["LiteMemcached"],
                },
            }
        },
    )

    print("Figure 14 Memcached memory bytes:")
    print(json.dumps(memory, indent=2))
    print("\nFigure 15 steady-state latency (ms, seconds 20..60):")
    for key, value in latency.items():
        print(f"  {key}: {value / 1000.0:.6f}")
    print("\nFigure 16 steady-state CPU (%):")
    print(json.dumps(cpu, indent=2))


if __name__ == "__main__":
    main()
