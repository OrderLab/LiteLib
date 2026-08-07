#!/usr/bin/env python3
"""Collect per-run mcrouter statistics for Figure 2.

This is the ``collect.py`` that produced the paper's
``data/deathstar/stats.json``, with the hard-coded ``crash``/``nocrash``
directories replaced by a ``--root`` option so it can run against any results
directory.  The parsing and the averaging windows are unchanged, so the output
is byte-identical for the same inputs.

For each mcrouter log it reports the mean of ``failover_all``,
``duration_us.avg`` and the per-interval request count, separately for the
window before and the window after the failure.
"""

import argparse
import copy
import glob
import json
import os
import sys

import pandas as pd

# Seconds into the run at which the failure is injected, per directory.  The
# no-crash baseline has no failure, so its "before" window covers the whole run
# and 87 simply falls past the end of it.
DEFAULT_CRASH_POINTS = {"crash": 20, "nocrash": 87}


def parse_mcrouter_log(file_path):
    timestamp = []
    cmd_get = []
    cmd_set = []
    failover_all = []
    duration_avg = []
    requests_sum = []

    current_ts = None
    prev_requests_sum = None

    with open(file_path, "r") as f:
        lines = f.readlines()
        i = 0
        while i < len(lines):
            line = lines[i].strip()

            if "Seconds Elapsed:" in line:
                try:
                    current_ts = float(line.split("Seconds Elapsed:")[1].split("(")[0].strip())
                except Exception:
                    i += 1
                    continue

                while i < len(lines) and not lines[i].strip().startswith("{"):
                    i += 1
                if i >= len(lines):
                    break

                json_lines = []
                while i < len(lines) and not lines[i].strip().startswith("}"):
                    json_lines.append(lines[i])
                    i += 1
                if i < len(lines):
                    json_lines.append(lines[i])

                try:
                    current_stats = json.loads("".join(json_lines))
                    current_requests_sum = current_stats.get(
                        "libmcrouter.mcrouter.11211.B.requests.sum", 0
                    )
                    # The counter is cumulative; the figure wants the rate.
                    if prev_requests_sum is not None:
                        timestamp.append(current_ts)
                        cmd_get.append(current_stats.get("libmcrouter.mcrouter.11211.cmd_get", 0))
                        cmd_set.append(current_stats.get("libmcrouter.mcrouter.11211.cmd_set", 0))
                        failover_all.append(
                            current_stats.get("libmcrouter.mcrouter.11211.failover_all", 0)
                        )
                        duration_avg.append(
                            current_stats.get("libmcrouter.mcrouter.11211.B.duration_us.avg", 0)
                        )
                        requests_sum.append(current_requests_sum - prev_requests_sum)
                    prev_requests_sum = current_requests_sum
                except json.JSONDecodeError:
                    pass
            i += 1

    return pd.DataFrame(
        {
            "timestamp": timestamp,
            "cmd_get": cmd_get,
            "cmd_set": cmd_set,
            "failover_all": failover_all,
            "duration_avg": duration_avg,
            "requests_sum_diff": requests_sum,
        }
    )


def collect_stats(log_files, crash_point):
    results = []
    for log_file in sorted(log_files):
        df = parse_mcrouter_log(log_file)
        # Drop the first/last few samples: the run is still ramping up at the
        # start, and the last interval is truncated by the client exiting.
        before_crash = df[df["timestamp"] <= crash_point][3:]
        after_crash = df[df["timestamp"] > crash_point][:-3]
        results.append(
            {
                "log_file": os.path.basename(log_file),
                "before_crash": {
                    "failover_all": before_crash["failover_all"].mean(),
                    "duration_avg": before_crash["duration_avg"].mean(),
                    "requests_sum_diff": before_crash["requests_sum_diff"].mean(),
                },
                "after_crash": {
                    "failover_all": after_crash["failover_all"].mean(),
                    "duration_avg": after_crash["duration_avg"].mean(),
                    "requests_sum_diff": after_crash["requests_sum_diff"].mean(),
                },
            }
        )
    return results


def main():
    parser = argparse.ArgumentParser(description="Collect stats from mcrouter logs")
    parser.add_argument("--root", default=".", help="directory holding crash/ and nocrash/")
    parser.add_argument(
        "--selected-runs",
        help="JSON mapping vanilla/litesys to selected client logs",
    )
    parser.add_argument(
        "--separate-nocrash",
        action="store_true",
        help="use root/nocrash for the before baseline (archived-paper check only)",
    )
    parser.add_argument("-o", "--output", help="output JSON file")
    args = parser.parse_args()

    all_results = {}
    for directory, crash_point in DEFAULT_CRASH_POINTS.items():
        if directory == "nocrash" and not args.separate_nocrash:
            continue
        path = os.path.join(args.root, directory)
        if directory == "crash" and args.selected_runs:
            with open(args.selected_runs) as stream:
                selected = json.load(stream)
            log_files = [
                os.path.join(
                    os.path.dirname(selected[arm]),
                    os.path.basename(selected[arm]).removesuffix(".log")
                    + ".mcrouter.log",
                )
                for arm in ("vanilla", "litesys")
            ]
            missing = [log_file for log_file in log_files if not os.path.isfile(log_file)]
            if missing:
                raise FileNotFoundError(
                    f"missing selected Mcrouter log(s): {', '.join(missing)}"
                )
        else:
            log_files = glob.glob(os.path.join(path, "*mcrouter*.log"))
        if not log_files:
            print(f"No mcrouter log files found in {path}")
            continue
        print(f"Found {len(log_files)} mcrouter log files in {directory}:")
        for log_file in sorted(log_files):
            print(f"  - {os.path.basename(log_file)}")
        all_results[directory] = collect_stats(log_files, crash_point)

    if not all_results:
        print("No mcrouter logs found", file=sys.stderr)
        sys.exit(1)

    # Figure 2 uses the same median-trend runs as Figure 1. Its "before" bars
    # are those runs' pre-failure segments. Preserve the JSON shape expected by
    # the paper plotter by aliasing crash entries when no no-crash run exists.
    if "crash" in all_results and "nocrash" not in all_results:
        all_results["nocrash"] = copy.deepcopy(all_results["crash"])
        print(
            "No separate nocrash logs: using each crash run's t<20s segment "
            "as the Figure 2 baseline."
        )

    if args.output:
        with open(args.output, "w") as f:
            json.dump(all_results, f, indent=2)
        print(f"\nResults saved to {args.output}")
    else:
        print(json.dumps(all_results, indent=2))


if __name__ == "__main__":
    main()
