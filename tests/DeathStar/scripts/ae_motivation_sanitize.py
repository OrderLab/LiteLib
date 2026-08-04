#!/usr/bin/env python3
"""Remove malformed/interleaved wrk2 sample lines before using paper plots.

Concurrent monitor output occasionally lands in the middle of a wrk2 line
(``98Completed ...``).  The hardened AE parser ignores those lines, but the
paper's original plotting parser raises.  Preserve every valid sample from
both the warm-up and measured segments and discard only malformed ``[...``
lines.
"""

import sys


def valid_sample(line):
    if not line.startswith("["):
        return True
    try:
        parts = line.strip().split("]")[1].split(",")
        float(line.split("[")[1].split("s")[0])
        float(parts[0].split(":")[1].strip().split()[0])
        return True
    except (IndexError, ValueError):
        return False


def main():
    if len(sys.argv) != 3:
        print("usage: ae_motivation_sanitize.py <input> <output>", file=sys.stderr)
        return 2
    with open(sys.argv[1]) as src, open(sys.argv[2], "w") as dst:
        for line in src:
            if valid_sample(line):
                dst.write(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
