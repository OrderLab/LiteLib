#!/usr/bin/env python3
import argparse
import csv
import glob
import re
from datetime import datetime


STAMP = re.compile(r"^[IWE](\d{4})\s+(\d{2}):(\d{2}):(\d{2})\.(\d{6})")


def wall_time(line):
    match = STAMP.match(line)
    if not match:
        raise ValueError(f"invalid timestamp: {line}")
    md, hour, minute, second, micros = match.groups()
    return datetime(
        2000, int(md[:2]), int(md[2:]), int(hour), int(minute),
        int(second), int(micros)
    ).timestamp()


def gap(path):
    lines = open(path, errors="replace").readlines()
    received = next(line for line in lines if "Received message kEnterEmergencyMode" in line)
    entered = next(line for line in lines if "Entered emergency mode" in line)
    return (wall_time(entered) - wall_time(received)) * 1000.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    rows = []
    for path in sorted(glob.glob(f"{args.root}/proxy-*.log")):
        rows.append({
            "application": "Redis",
            "solution": "LiteLib",
            "setting": "proxy",
            "repetition": int(path.rsplit("-", 1)[1].split(".")[0]),
            "gap_ms": gap(path),
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
