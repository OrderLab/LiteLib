#!/usr/bin/env bash

set -e
set -x

usage() {
  echo "Usage: $0 <remote_host> [port] [samples]"
  echo "Example: $0 node2"
  echo "Example: $0 node2 9999 50"
}

REMOTE="${1:-}"
PORT="${2:-9999}"
SAMPLES="${3:-50}"

if [[ -z "$REMOTE" ]]; then usage; exit 1; fi
if ! [[ "$SAMPLES" =~ ^[0-9]+$ ]] || [[ "$SAMPLES" -le 0 ]]; then
  echo "ERROR: samples must be a positive integer"
  exit 1
fi

command -v ssh >/dev/null 2>&1 || { echo "ERROR: missing ssh"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "ERROR: missing python3 locally"; exit 1; }

REMOTE_PID=""
REMOTE_LOG="/tmp/offset_responder_$(id -u)_${PORT}.log"

start_remote() {
  ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new "$REMOTE" "command -v python3 >/dev/null 2>&1" >/dev/null

  REMOTE_PID="$(ssh -o BatchMode=yes "$REMOTE" "nohup python3 -u - <<'PY' >'${REMOTE_LOG}' 2>&1 & echo \$!
import socket, time
host = '0.0.0.0'
port = int('${PORT}')
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind((host, port))
print(f'listening on {host}:{port}', flush=True)
while True:
    data, addr = s.recvfrom(2048)
    ts_us = time.time_ns() // 1000
    s.sendto(str(ts_us).encode(), addr)
PY")"

  [[ "$REMOTE_PID" =~ ^[0-9]+$ ]] || {
    echo "ERROR: failed to start responder on $REMOTE. PID='$REMOTE_PID'"
    echo "Check remote log: ssh $REMOTE 'tail -n 50 ${REMOTE_LOG}'"
    exit 2
  }
}

stop_remote() {
  if [[ -n "${REMOTE_PID:-}" ]]; then
    ssh -o BatchMode=yes "$REMOTE" "kill ${REMOTE_PID} >/dev/null 2>&1 || true; sleep 0.05; kill -9 ${REMOTE_PID} >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
  fi
}

trap stop_remote EXIT INT TERM

echo "Starting remote UDP responder on ${REMOTE}:${PORT} ..."
start_remote
echo "Remote responder PID: ${REMOTE_PID}"
echo "Remote log (on ${REMOTE}): ${REMOTE_LOG}"
echo

python3 - <<PY
import socket, time, statistics, sys

remote = "${REMOTE}"
port = int("${PORT}")
samples = int("${SAMPLES}")

def now_us():
    return time.time_ns() // 1000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(1.0)

rtts = []
offsets = []

best = None  # (rtt_us, offset_us)

for i in range(samples):
    try:
        t1 = now_us()
        sock.sendto(b"ping", (remote, port))
        data, _ = sock.recvfrom(2048)
        t3 = now_us()
        t2 = int(data.decode().strip())

        rtt_us = t3 - t1
        offset_us = (t2 - t1) - (rtt_us // 2)   # offset = REMOTE - LOCAL

        rtts.append(rtt_us)
        offsets.append(offset_us)

        if best is None or rtt_us < best[0]:
            best = (rtt_us, offset_us)
    except Exception as e:
        print(f"WARNING: sample {i+1}/{samples} failed: {e}", file=sys.stderr)

if not rtts:
    print("ERROR: all samples failed (no replies).", file=sys.stderr)
    sys.exit(3)

rtt_min, offset_at_min = best
rtt_med = int(statistics.median(rtts))
offset_med = int(statistics.median(offsets))

# Also show a trimmed mean-ish view by using median absolute deviation window (optional simple robustness)
abs_off = [abs(x - offset_med) for x in offsets]
mad = int(statistics.median(abs_off)) if abs_off else 0

print(f"samples_ok={len(rtts)}/{samples}")
print()
print(f"BEST (min RTT) -> rtt_us={rtt_min}  offset_us={offset_at_min}")
print("Meaning: offset = (REMOTE clock) minus (LOCAL clock)")
print("         remote_time ≈ local_time + offset")
print(f"best_abs_offset_ms={abs(offset_at_min)/1000:.3f}")
print(f"best_rule_of_thumb_error_ms~RTT/2={(rtt_min/2)/1000:.3f}")
print()
print(f"MEDIAN -> rtt_us={rtt_med}  offset_us={offset_med}")
print(f"median_abs_offset_ms={abs(offset_med)/1000:.3f}")
print(f"offset_mad_us={mad}   (median absolute deviation)")
PY
