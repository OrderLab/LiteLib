#!/usr/bin/env python3
"""Check that Figure 1 shows the trend claimed in the paper.

The claim (Section 2) is:

  * **Vanilla**: after one Memcached instance crashes, client latency keeps
    climbing and shows no sign of stabilising by the end of the run.
  * **LiteLib**: latency spikes briefly and then falls back to roughly its
    pre-failure level.

Exits 0 when both hold, 1 otherwise, so it can gate an automated run.
"""

import sys

import numpy as np
import pandas as pd

CRASH_TIME = 20.0
# LiteLib must return to within this multiple of its pre-failure latency.
RECOVERY_FACTOR = 3.0
# Vanilla's latency in the final window must exceed its first post-crash
# window by at least this factor to count as "still climbing".
GROWTH_FACTOR = 1.5


def parse_log_file(file_path):
    """Parse a wrk2 client log.  Identical to the paper's plotting parser."""
    timestamp, throughput, latency = [], [], []
    first_segment_removed = False
    prev_ts = None

    with open(file_path, "r") as f:
        for line in f:
            if not line.startswith("["):
                continue
            try:
                parts = line.strip().split("]")[1].split(",")
                ts = float(line.split("[")[1].split("s")[0])
            except (IndexError, ValueError):
                # wrk2 interleaves its latency histogram with the per-second
                # samples; skip anything that is not a sample line.
                continue

            # The run starts with a warm-up pass; drop it.
            if not first_segment_removed:
                if prev_ts is not None and ts < prev_ts:
                    first_segment_removed = True
                else:
                    prev_ts = ts
                    continue

            try:
                tput = float(parts[0].split(":")[1].strip().split()[0])
            except (IndexError, ValueError):
                continue
            lat = np.nan
            for part in parts:
                if "mean=" in part:
                    try:
                        s = part.split("mean=")[1].split(",")[0].strip()
                        lat = float(s[:-1]) * 1000 if s.endswith("s") else float(s)
                    except Exception:
                        lat = np.nan
                    break
            timestamp.append(ts)
            throughput.append(tput)
            latency.append(lat)

    return pd.DataFrame({"timestamp": timestamp, "throughput": throughput, "latency": latency})


def summarise(name, path):
    df = parse_log_file(path)
    d = df[df["latency"].notna()]
    if d.empty:
        print(f"  {name}: no latency samples in {path}")
        return None

    pre = d[d["timestamp"] <= CRASH_TIME]["latency"].mean()
    end_ts = d["timestamp"].max()
    print(f"\n  === {name} ===   pre-failure mean latency: {pre:8.0f} ms")

    windows = []
    for lo in range(int(CRASH_TIME), int(end_ts), 20):
        hi = min(lo + 20, end_ts)
        w = d[(d["timestamp"] > lo) & (d["timestamp"] <= hi)]["latency"]
        if len(w):
            windows.append((lo, hi, w.mean()))
            print(f"    t={lo:>3.0f}-{hi:<3.0f}s   mean={w.mean():9.0f} ms")

    final = d.iloc[-1]["latency"]
    print(f"    final @ t={end_ts:.0f}s: {final:.0f} ms")
    return {"pre": pre, "windows": windows, "final": final}


def main():
    if len(sys.argv) != 3:
        print("usage: ae_motivation_trend.py <vanilla_client.log> <litesys_client.log>")
        return 2

    vanilla = summarise("vanilla (baseline)", sys.argv[1])
    litesys = summarise("litesys (LiteLib)", sys.argv[2])
    if vanilla is None or litesys is None:
        return 1

    print("\n  --- expected trend ---")
    ok = True

    # Vanilla: latency should keep climbing after the failure.  Require a
    # sustained trend (at least two of the three adjacent windows increase),
    # not one spike followed by recovery.
    if len(vanilla["windows"]) >= 2:
        means = [w[2] for w in vanilla["windows"]]
        first, last = means[0], means[-1]
        increases = sum(b > a * 1.10 for a, b in zip(means, means[1:]))
        climbing = (
            last > first * GROWTH_FACTOR
            and last > vanilla["pre"] * 5
            and increases >= 2
        )
        print(
            f"    vanilla keeps degrading: {first:.0f} -> {last:.0f} ms "
            f"({last / first:.1f}x, {increases}/3 rising windows)  "
            f"{'OK' if climbing else 'NOT SEEN'}"
        )
        ok &= climbing
    else:
        print("    vanilla: too few samples after the failure to judge the trend")
        ok = False

    # LiteLib: latency should come back down close to its pre-failure level.
    litesys_tail = litesys["windows"][-1][2]
    recovered = litesys_tail <= litesys["pre"] * RECOVERY_FACTOR
    print(
        f"    litesys recovers:        final window {litesys_tail:.0f} ms vs "
        f"pre-failure {litesys['pre']:.0f} ms  "
        f"({litesys_tail / litesys['pre']:.1f}x)  "
        f"{'OK' if recovered else 'NOT SEEN'}"
    )
    ok &= recovered

    # And it should end up dramatically better than the baseline.
    if litesys["final"] > 0:
        ratio = vanilla["final"] / litesys["final"]
        print(f"    final latency gap:       vanilla is {ratio:.0f}x higher than litesys")

    print(f"\n  ==> Figure 1 trend {'MATCHES' if ok else 'DOES NOT MATCH'} the paper")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
