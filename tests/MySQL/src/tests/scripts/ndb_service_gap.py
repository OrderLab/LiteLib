#!/usr/bin/env python3
import argparse
import json
import sys
from dataclasses import dataclass
from typing import Optional


@dataclass
class Ev:
    line_no: int
    start_us: int
    end_us: int
    server: str
    client: str


def iter_json_lines(path: str):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            yield i, obj


def main():
    ap = argparse.ArgumentParser(
        description="Compute global failover gap: last OK to node2 before kill -> first OK to node3 after kill."
    )
    ap.add_argument("--logfile", default="/var/lib/proxysql/queries.log.00000001", help="ProxySQL events log file (JSON lines).")
    ap.add_argument("--kill-us", type=int, required=True,
                    help="Kill timestamp in microseconds since epoch (get via: date +%%s%%6N on ProxySQL host).")
    ap.add_argument("--node2", default="node2:50000", help="Backend label for node2 (matches 'server').")
    ap.add_argument("--node3", default="node3:50000", help="Backend label for node3 (matches 'server').")
    ap.add_argument("--event", default="COM_QUERY", help="Only consider this event type (default: COM_QUERY).")
    args = ap.parse_args()

    kill_us = args.kill_us

    last_ok_node2: Optional[Ev] = None
    first_ok_node3: Optional[Ev] = None

    for line_no, obj in iter_json_lines(args.logfile):
        if args.event and obj.get("event") != args.event:
            continue

        server = obj.get("server")
        errno = obj.get("errno")
        if server is None or errno is None:
            continue
        if errno != 0:
            continue  # only successful queries

        start_us = obj.get("starttime_timestamp_us")
        end_us = obj.get("endtime_timestamp_us")
        if start_us is None or end_us is None:
            continue

        client = obj.get("client", "")

        # last OK routed to node2 before kill
        if end_us < kill_us:
            if last_ok_node2 is None or end_us > last_ok_node2.end_us:
                last_ok_node2 = Ev(line_no=line_no, start_us=start_us, end_us=end_us,
                                   server=server, client=client)

        # first OK routed to node3 after kill
        if start_us > kill_us:
            if first_ok_node3 is None or start_us < first_ok_node3.start_us:
                first_ok_node3 = Ev(line_no=line_no, start_us=start_us, end_us=end_us,
                                    server=server, client=client)

    if last_ok_node2 is None:
        print("ERROR: did not find any successful query before kill_us.", file=sys.stderr)
        return 2
    if first_ok_node3 is None:
        print("ERROR: did not find any successful query after kill_us.", file=sys.stderr)
        return 3

    gap_us = first_ok_node3.start_us - last_ok_node2.end_us

    print("Global gap (us):", gap_us)
    print()
    print("Last OK before kill:")
    print(f"  line={last_ok_node2.line_no} client={last_ok_node2.client} "
          f"end_us={last_ok_node2.end_us} start_us={last_ok_node2.start_us} server={last_ok_node2.server}")
    print("First OK after kill:")
    print(f"  line={first_ok_node3.line_no} client={first_ok_node3.client} "
          f"start_us={first_ok_node3.start_us} end_us={first_ok_node3.end_us} server={first_ok_node3.server}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
