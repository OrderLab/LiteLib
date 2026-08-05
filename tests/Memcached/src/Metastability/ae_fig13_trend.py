#!/usr/bin/env python3
"""Validate Figure 13's qualitative claims and choose the plotted window."""

import argparse
import math
import sys

import numpy as np

ARRIVAL_RATE = 400
TOTAL_TIME = 300
WINDOW = 5


def process(path):
    completions = np.zeros(TOTAL_TIME + 1)
    hits = np.zeros(TOTAL_TIME + 1)
    stale = np.zeros(TOTAL_TIME + 1)
    with open(path) as f:
        start = int(f.readline().strip())
        for line in f:
            fields = line.split()
            if len(fields) < 4:
                continue
            req_start, duration, cache_hit, error = map(int, fields[:4])
            second = math.ceil((req_start + duration - start) / 1_000_000_000)
            if not 0 <= second <= TOTAL_TIME:
                continue
            completions[second] += 1
            hits[second] += cache_hit
            stale[second] += error == 2

    crash = int(np.argmin(completions[10:]) + 10)
    return {
        "crash": crash,
        "hits": hits,
        "stale_by_second": stale,
    }

def recovery_time(data, crash):
    """Absolute second ending the first sustained 5s >=90% window."""
    rolling = np.convolve(
        data["hits"] / ARRIVAL_RATE, np.ones(WINDOW) / WINDOW, mode="valid"
    )
    candidates = np.where(rolling[crash + 1 :] >= 0.9)[0]
    if not len(candidates):
        return None
    window_start = int(candidates[0] + crash + 1)
    return window_start + WINDOW - 1


def fmt_recovery(value):
    return "not within experiment" if value is None else f"{value}s"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--window", action="store_true")
    parser.add_argument("full")
    parser.add_argument("lite")
    parser.add_argument("checkpoint")
    args = parser.parse_args()
    full, lite, checkpoint = map(
        process, (args.full, args.lite, args.checkpoint)
    )
    # The visible outage in checkpoint/LiteLib can be too short to become the
    # minimum-completion second. Use vanilla's unambiguous crash time for all.
    crash = full["crash"]
    full_recovery = recovery_time(full, crash)
    lite_recovery = recovery_time(lite, crash)
    checkpoint_recovery = recovery_time(checkpoint, crash)

    if lite_recovery is None:
        if args.window:
            print(TOTAL_TIME)
            return 0
        window_end = TOTAL_TIME
    else:
        window_end = min(TOTAL_TIME, lite_recovery + 20)

    if args.window:
        print(window_end)
        return 0

    vanilla_tail = (
        full["hits"][max(0, window_end - 19) : window_end + 1] / ARRIVAL_RATE
    )
    vanilla_tail_avg = float(np.mean(vanilla_tail))
    vanilla_tail_slope = float(
        np.polyfit(np.arange(len(vanilla_tail)), vanilla_tail, 1)[0]
    )
    checkpoint_stale = int(
        checkpoint["stale_by_second"][: window_end + 1].sum()
    )
    lite_recovery_delta = (
        None if lite_recovery is None else lite_recovery - crash
    )
    checkpoint_recovery_delta = (
        None
        if checkpoint_recovery is None
        else checkpoint_recovery - crash
    )

    print("\n==> Figure 13 qualitative trend")
    print(
        f"  vanilla:    final-window throughput {vanilla_tail_avg * 100:.1f}%, "
        f"slope {vanilla_tail_slope:+.3f}/s"
    )
    print(f"  LiteLib:    90% recovery {fmt_recovery(lite_recovery_delta)}")
    print(
        f"  checkpoint: 90% recovery {fmt_recovery(checkpoint_recovery_delta)}, "
        f"stale responses in plotted window {checkpoint_stale}"
    )

    vanilla_bad = vanilla_tail_avg < 0.5
    vanilla_stable = abs(vanilla_tail_slope) < 0.01
    lite_recovers = (
        lite_recovery is not None and lite_recovery <= window_end
    )
    checkpoint_fast = (
        checkpoint_recovery_delta is not None
        and checkpoint_recovery_delta <= 30
    )
    checkpoint_has_stale = checkpoint_stale >= 100

    checks = [
        ("vanilla remains at very bad throughput", vanilla_bad),
        ("vanilla is stable in that degraded state", vanilla_stable),
        ("LiteLib recovers within the experiment window", lite_recovers),
        ("checkpoint recovers very quickly", checkpoint_fast),
        ("checkpoint returns substantial stale data", checkpoint_has_stale),
    ]
    for label, ok in checks:
        print(f"  [{' OK ' if ok else 'FAIL'}] {label}")
    ok = all(value for _, value in checks)
    print(f"\n  ==> Figure 13 trend {'MATCHES' if ok else 'DOES NOT MATCH'} the paper")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
