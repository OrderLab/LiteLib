#!/usr/bin/env python3
import argparse
import csv
import glob
import re
from datetime import datetime


STATUS = re.compile(
    r" (\d+) sec: .*?\[READ: Count=(\d+).*?\].*?"
    r"\[UPDATE: Count=(\d+)"
)
GLOG = re.compile(r"^[IWE](\d{4})\s+(\d{2}):(\d{2}):(\d{2})\.(\d{6})")


def zero_gap(path, crash_after):
    rows = []
    for line in open(path, errors="replace"):
        match = STATUS.search(line)
        if match:
            rows.append(
                (
                    int(match.group(1)),
                    int(match.group(2)) + int(match.group(3)),
                )
            )
    sections = []
    current = []
    last_second = -1
    for row in rows:
        if row[0] <= last_second and current:
            sections.append(current)
            current = []
        current.append(row)
        last_second = row[0]
    if current:
        sections.append(current)
    section = max(
        enumerate(sections), key=lambda item: (len(item[1]), item[0])
    )[1]
    after = [
        (second, value)
        for second, value in section
        if second >= crash_after
    ]
    zero_runs = []
    start = None
    for second, value in after:
        if value == 0 and start is None:
            start = second
        elif value > 0 and start is not None:
            zero_runs.append((start, second))
            start = None
    if not zero_runs:
        raise ValueError(f"no zero-throughput interval in {path}")
    start, end = max(zero_runs, key=lambda item: item[1] - item[0])
    return float(end - (start - 1))


def wall_time(line):
    match = GLOG.match(line)
    if not match:
        raise ValueError(f"invalid glog timestamp: {line}")
    md, hour, minute, second, micros = match.groups()
    stamp = datetime(
        2000, int(md[:2]), int(md[2:]), int(hour), int(minute),
        int(second), int(micros)
    )
    return stamp.timestamp()


def barrier_gap(path):
    lines = open(path, errors="replace").readlines()
    replay = max(i for i, line in enumerate(lines) if "Replay took" in line)
    barriers = [
        line for line in lines[:replay]
        if "Barrier completed" in line
    ]
    if len(barriers) < 2:
        raise ValueError(f"missing replay barriers in {path}")
    return (wall_time(barriers[-1]) - wall_time(barriers[-2])) * 1000.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--output", required=True)
    parser.add_argument("--crash-after", type=int, default=30)
    args = parser.parse_args()
    rows = []
    for label in ("ap-30s", "ap-5s"):
        for path in sorted(glob.glob(f"{args.root}/benchmark-{label}-*.log")):
            rows.append({
                "application": "Redis",
                "solution": "active-passive",
                "setting": "30s detection" if label == "ap-30s" else "5s detection",
                "repetition": int(path.rsplit("-", 1)[1].split(".")[0]),
                "gap_ms": zero_gap(path, args.crash_after) * 1000.0,
                "source": path,
            })
    for path in sorted(glob.glob(f"{args.root}/lite-embedded-*.log")):
        rows.append({
            "application": "Redis",
            "solution": "LiteLib",
            "setting": "embedded",
            "repetition": int(path.rsplit("-", 1)[1].split(".")[0]),
            "gap_ms": barrier_gap(path),
            "source": path,
        })
    with open(args.output, "w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    for row in rows:
        print(row)


if __name__ == "__main__":
    main()
