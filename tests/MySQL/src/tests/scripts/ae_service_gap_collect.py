#!/usr/bin/env python3
import argparse
import csv
import glob
import json
import re
from datetime import datetime


TPS = re.compile(r"\[\s*(\d+)s \].*tps:\s*([0-9.]+)")
KILL = {
    "t1": re.compile(r"t1=(\d+)"),
    "tdead": re.compile(r"tdead=(\d+)"),
}
OFFSET = re.compile(r"BEST \(min RTT\) -> .*offset_us=(-?\d+)")
END = re.compile(r"FAILOVER_END .* start_us=(\d+)")
STAMP = re.compile(r"^[IWE](\d{4})\s+(\d{2}):(\d{2}):(\d{2})\.(\d{6})")


def zero_gap(path):
    rows = []
    for line in open(path, errors="replace"):
        match = TPS.search(line)
        if match:
            rows.append((int(match.group(1)), float(match.group(2))))
    zero_runs = []
    start = None
    for second, value in rows:
        if value == 0 and start is None:
            start = second
        elif value > 0 and start is not None:
            zero_runs.append((start, second))
            start = None
    if not zero_runs:
        raise ValueError(f"no zero-throughput interval in {path}")
    start, end = max(zero_runs, key=lambda item: item[1] - item[0])
    return float(end - (start - 1)) * 1000.0


def kill_time(path, field="tdead"):
    match = KILL[field].search(open(path).read())
    if not match:
        raise ValueError(f"no kill timestamp in {path}")
    return int(match.group(1))


def ndb_client_gap(sysbench, kill, offset):
    text = open(sysbench, errors="replace").read()
    starts = [int(value) for value in END.findall(text)]
    if not starts:
        raise ValueError(f"no FAILOVER_END in {sysbench}")
    offset_match = OFFSET.search(open(offset).read())
    if not offset_match:
        raise ValueError(f"no timing offset in {offset}")
    remote_minus_local = int(offset_match.group(1))
    return (min(starts) - kill_time(kill, "t1") - remote_minus_local) / 1000.0


def ndb_proxy_gap(query_log, kill):
    killed = kill_time(kill)
    before = []
    after = []
    for line in open(query_log, errors="replace"):
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if row.get("event") != "COM_QUERY" or row.get("errno") != 0:
            continue
        start = row.get("starttime_timestamp_us")
        end = row.get("endtime_timestamp_us")
        if start is None or end is None:
            continue
        if end < killed:
            before.append(end)
        elif start > killed:
            after.append(start)
    if not before or not after:
        raise ValueError(f"missing successful queries around crash in {query_log}")
    return (min(after) - max(before)) / 1000.0


def wall_time(line):
    match = STAMP.match(line)
    if not match:
        raise ValueError(f"invalid timestamp: {line}")
    md, hour, minute, second, micros = match.groups()
    return datetime(
        2000, int(md[:2]), int(md[2:]), int(hour), int(minute),
        int(second), int(micros)
    ).timestamp()


def lite_gap(path):
    lines = open(path, errors="replace").readlines()
    received = next(line for line in lines if "Received message kEnterEmergencyMode" in line)
    entered = next(line for line in lines if "Entered emergency mode" in line)
    return (wall_time(entered) - wall_time(received)) * 1000.0


def repetition(path):
    return int(path.rsplit("-", 1)[1].split(".")[0])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    rows = []
    for path in sorted(glob.glob(f"{args.root}/sysbench-ap-*.log")):
        rows.append({
            "application": "MySQL",
            "solution": "active-passive",
            "setting": "binlog replication",
            "repetition": repetition(path),
            "gap_ms": zero_gap(path),
            "source": path,
        })
    for path in sorted(glob.glob(f"{args.root}/sysbench-ndb-client-*.log")):
        rep = repetition(path)
        rows.append({
            "application": "MySQL",
            "solution": "active-active",
            "setting": "NDB client failover",
            "repetition": rep,
            "gap_ms": ndb_client_gap(
                path,
                f"{args.root}/kill-ndb-client-{rep}.txt",
                f"{args.root}/offset-ndb-client-{rep}.txt",
            ),
            "source": path,
        })
    for path in sorted(glob.glob(f"{args.root}/queries-ndb-proxy-*.jsonl")):
        rep = repetition(path)
        rows.append({
            "application": "MySQL",
            "solution": "active-active",
            "setting": "NDB proxy failover",
            "repetition": rep,
            "gap_ms": ndb_proxy_gap(
                path, f"{args.root}/kill-ndb-proxy-{rep}.txt"
            ),
            "source": path,
        })
    for path in sorted(glob.glob(f"{args.root}/lite-lite-proxy-*.log")):
        rows.append({
            "application": "MySQL",
            "solution": "LiteLib",
            "setting": "proxy",
            "repetition": repetition(path),
            "gap_ms": lite_gap(path),
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
