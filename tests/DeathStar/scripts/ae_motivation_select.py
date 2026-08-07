#!/usr/bin/env python3
"""Select median-trend Figure 1 runs from a repetition set.

Rank each arm's repetitions by its trend metric and select the middle run:

* vanilla: score sustained post-failure latency growth;
* LiteLib: score late recovery and stability.

Prints two tab-separated lines: ``vanilla<TAB>path`` and
``litesys<TAB>path``.
"""

import glob
import math
import os
import re
import sys

from ae_motivation_trend import parse_log_file


def client_logs(directory, prefix):
    pattern = re.compile(rf"{prefix}_\d{{8}}_\d{{6}}\.log$")
    return [p for p in sorted(glob.glob(os.path.join(directory, f"{prefix}_*.log")))
            if pattern.search(p)]


def metrics(path):
    df = parse_log_file(path)
    df = df[df["latency"].notna()]
    if df.empty:
        return None
    pre = df[df["timestamp"] <= 20]["latency"].mean()
    windows = []
    for lo, hi in ((20, 40), (40, 60), (60, 80), (80, 90)):
        w = df[(df["timestamp"] > lo) & (df["timestamp"] <= hi)]["latency"]
        if w.empty:
            return None
        windows.append(float(w.mean()))
    return pre, windows


def vanilla_score(path):
    m = metrics(path)
    if not m:
        return float("-inf")
    pre, w = m
    increases = sum(b > a * 1.10 for a, b in zip(w, w[1:]))
    growth = w[-1] / max(w[0], 1.0)
    degradation = w[-1] / max(pre, 1.0)
    # Give sustained increases priority over one isolated spike.
    return increases * 100 + math.log1p(growth) * 10 + math.log1p(degradation)


def litesys_score(path):
    m = metrics(path)
    if not m:
        return float("inf")
    pre, w = m
    # The final 20-second window, not one lucky final sample, must recover.
    recovery = w[-1] / max(pre, 1.0)
    late_stability = abs(w[-1] - w[-2]) / max(pre, 1.0)
    return recovery + 0.1 * late_stability


def median_by_score(paths, score):
    ranked = sorted(
        ((score(path), os.path.basename(path), path) for path in paths),
        key=lambda item: (item[0], item[1]),
    )
    return ranked[len(ranked) // 2][2]


def main():
    if len(sys.argv) != 2:
        print("usage: ae_motivation_select.py <crash-results-dir>", file=sys.stderr)
        return 2
    directory = sys.argv[1]
    vanilla = client_logs(directory, "vanilla")
    litesys = client_logs(directory, "litesys")
    if not vanilla or not litesys:
        print("missing vanilla or litesys client logs", file=sys.stderr)
        return 1
    print(f"vanilla\t{median_by_score(vanilla, vanilla_score)}")
    print(f"litesys\t{median_by_score(litesys, litesys_score)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
