#!/usr/bin/env python3
"""Reject a LiteLib run that does not recover its pre-failure latency."""

import math
import sys


CRASH_TIME = 20.0
RECOVERY_FACTOR = 3.0
WINDOW_SECONDS = 20.0


def measured_latency_samples(path):
    segments = [[]]
    previous_timestamp = None

    with open(path, encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if not line.startswith("[") or "mean=" not in line:
                continue
            try:
                timestamp = float(line.split("[", 1)[1].split("s", 1)[0])
                latency = float(line.split("mean=", 1)[1].split(",", 1)[0])
            except (IndexError, ValueError):
                continue

            if (
                previous_timestamp is not None
                and timestamp < previous_timestamp
            ):
                segments.append([])
            segments[-1].append((timestamp, latency))
            previous_timestamp = timestamp

    return segments[-1]


def mean(values):
    return sum(values) / len(values)


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} CLIENT_LOG", file=sys.stderr)
        return 2

    samples = measured_latency_samples(sys.argv[1])
    if not samples:
        print("no measured latency samples found", file=sys.stderr)
        return 1

    pre = [latency for timestamp, latency in samples if timestamp <= CRASH_TIME]
    end_time = samples[-1][0]
    tail = [
        latency
        for timestamp, latency in samples
        if timestamp > end_time - WINDOW_SECONDS
    ]
    if len(pre) < 10 or len(tail) < 10:
        print(
            f"insufficient latency samples (pre={len(pre)}, tail={len(tail)})",
            file=sys.stderr,
        )
        return 1

    pre_mean = mean(pre)
    tail_mean = mean(tail)
    ratio = tail_mean / pre_mean if pre_mean > 0 else math.inf
    print(
        "LiteLib latency recovery: "
        f"pre={pre_mean:.1f} ms, final-window={tail_mean:.1f} ms "
        f"({ratio:.1f}x)"
    )
    if not math.isfinite(ratio) or ratio > RECOVERY_FACTOR:
        print(
            f"final-window latency exceeds {RECOVERY_FACTOR:.1f}x pre-failure",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
