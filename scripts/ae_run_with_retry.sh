#!/bin/bash
# Run one documented experiment with a conservative timeout and one retry.
set -euo pipefail

[ "$#" -ge 3 ] || {
  echo "usage: $0 TIMEOUT_SECONDS MAX_ATTEMPTS COMMAND [ARG...]" >&2
  exit 2
}

TIMEOUT_SECONDS=$1
MAX_ATTEMPTS=$2
shift 2
COMMAND=$1
CLEANUP=${COMMAND%_run.sh}_cleanup.sh

for attempt in $(seq 1 "${MAX_ATTEMPTS}"); do
  echo "==> experiment attempt ${attempt}/${MAX_ATTEMPTS} (timeout ${TIMEOUT_SECONDS}s)"
  set +e
  AE_RUN_INNER=1 timeout --signal=TERM --kill-after=60s \
    "${TIMEOUT_SECONDS}s" "$@"
  rc=$?
  set -e
  if [ "${rc}" -eq 0 ]; then
    exit 0
  fi

  if [ "${rc}" -eq 124 ] || [ "${rc}" -eq 137 ]; then
    echo "  [WARN] experiment attempt ${attempt} timed out" >&2
  else
    echo "  [WARN] experiment attempt ${attempt} failed (exit ${rc})" >&2
  fi
  [ "${attempt}" -lt "${MAX_ATTEMPTS}" ] || exit "${rc}"

  if [ -x "${CLEANUP}" ]; then
    echo "==> cleaning runtime before retry"
    timeout --signal=TERM --kill-after=30s 900s "${CLEANUP}" || {
      echo "  [FAIL] cleanup before retry failed" >&2
      exit 1
    }
  fi
  sleep 10
done
