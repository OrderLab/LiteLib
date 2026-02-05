#!/usr/bin/env python3
"""
Count successful COM_QUERY routed to node2/node3 in:
  [kill_us - pre_us, kill_us) and [kill_us, kill_us + post_us)

Usage:
  python3 count_pre_post.py /var/lib/proxysql/queries.log.00000001 --kill-us 1769463633069056
"""

import argparse
import json
from dataclasses import dataclass
from typing import Dict, Tuple


@dataclass
class Counts:
    node2: int = 0
    node3: int = 0

    def add(self, server: str, node2: str, node3: str):
        if server == node2:
            self.node2 += 1
        elif server == node3:
            self.node3 += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--logfile", default="/var/lib/proxysql/queries.log.00000001", help="ProxySQL events log (JSON lines)")
    ap.add_argument("--kill-us", type=int, required=True, help="Kill timestamp in us (date +%s%6N on ProxySQL host)")
    ap.add_argument("--pre-sec", type=float, default=5.0, help="Seconds before kill to count (default 5)")
    ap.add_argument("--post-sec", type=float, default=5.0, help="Seconds after kill to count (default 5)")
    ap.add_argument("--node2", default="node2:50000", help="Server label for node2")
    ap.add_argument("--node3", default="node3:50000", help="Server label for node3")
    ap.add_argument("--event", default="COM_QUERY", help="Event type to count (default COM_QUERY)")
    ap.add_argument("--only-ok", action="store_true", default=True,
                    help="Count only errno==0 (default True).")
    ap.add_argument("--include-non-ok", dest="only_ok", action="store_false",
                    help="Include errno!=0 too (disables only-ok).")
    args = ap.parse_args()

    kill_us = args.kill_us
    pre_us = int(args.pre_sec * 1_000_000)
    post_us = int(args.post_sec * 1_000_000)

    pre_start = kill_us - pre_us
    pre_end = kill_us
    post_start = kill_us
    post_end = kill_us + post_us

    pre = Counts()
    post = Counts()
    other_pre = 0
    other_post = 0

    with open(args.logfile, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                o = json.loads(line)
            except json.JSONDecodeError:
                continue

            if args.event and o.get("event") != args.event:
                continue

            errno = o.get("errno")
            if args.only_ok and errno != 0:
                continue

            server = o.get("server")
            if not server:
                continue

            # Use start timestamp for "routed/served" attribution
            ts = o.get("starttime_timestamp_us")
            if ts is None:
                continue

            if pre_start <= ts < pre_end:
                if server in (args.node2, args.node3):
                    pre.add(server, args.node2, args.node3)
                else:
                    other_pre += 1
            elif post_start <= ts < post_end:
                if server in (args.node2, args.node3):
                    post.add(server, args.node2, args.node3)
                else:
                    other_post += 1

    def fmt_window(a: int, b: int) -> str:
        return f"[{a}, {b}) us"

    print("Settings:")
    print(f"  logfile   = {args.logfile}")
    print(f"  kill_us   = {kill_us}")
    print(f"  node2     = {args.node2}")
    print(f"  node3     = {args.node3}")
    print(f"  event     = {args.event}")
    print(f"  only_ok   = {args.only_ok}")
    print()

    print("Window (5s before):", fmt_window(pre_start, pre_end))
    print(f"  node2_ok = {pre.node2}")
    print(f"  node3_ok = {pre.node3}")
    if other_pre:
        print(f"  other_ok = {other_pre}")
    print(f"  total_ok = {pre.node2 + pre.node3 + other_pre}")
    print()

    print("Window (5s after):", fmt_window(post_start, post_end))
    print(f"  node2_ok = {post.node2}")
    print(f"  node3_ok = {post.node3}")
    if other_post:
        print(f"  other_ok = {other_post}")
    print(f"  total_ok = {post.node2 + post.node3 + other_post}")
    print()

    # Extra: share percentages (helps interpretation)
    def pct(x: int, total: int) -> str:
        return "n/a" if total == 0 else f"{100.0 * x / total:.1f}%"

    pre_total = pre.node2 + pre.node3 + other_pre
    post_total = post.node2 + post.node3 + other_post

    print("Share (% of counted queries):")
    print(f"  pre : node2={pct(pre.node2, pre_total)}  node3={pct(pre.node3, pre_total)}")
    print(f"  post: node2={pct(post.node2, post_total)}  node3={pct(post.node3, post_total)}")


if __name__ == "__main__":
    main()
