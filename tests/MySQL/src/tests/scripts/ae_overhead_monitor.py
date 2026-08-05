#!/usr/bin/env python3
import json
import sys
import time

import psutil


duration = int(sys.argv[1])
output = sys.argv[2]
tokens = ("mysql", "LiteMySQL", "ndb", "proxysql", "orchestrator")

with open(output, "w") as stream:
    for second in range(duration + 1):
        row = {"time": second}
        for process in psutil.process_iter(["name"]):
            name = process.info["name"] or ""
            if not any(token.lower() in name.lower() for token in tokens):
                continue
            try:
                with process.oneshot():
                    entry = row.setdefault(name, {"cpu": 0.0, "mem": 0.0})
                    entry["cpu"] += process.cpu_percent(interval=None)
                    entry["mem"] += process.memory_info().rss
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        stream.write(json.dumps(row) + "\n")
        stream.flush()
        if second < duration:
            time.sleep(max(0, 1 - time.time() % 1))
