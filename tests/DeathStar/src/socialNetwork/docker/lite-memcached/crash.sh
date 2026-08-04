#!/bin/bash
#
# Inject the failure: hand over to LiteMemcached, then kill memcached.
#
# The lite_cli call is what puts LiteMemcached into serving mode.  Without it
# the LiteLib arm degrades exactly like the vanilla one, because nothing ever
# takes over from the memcached instance being killed.
#
# Figures 1/2 use the separate, non-embedded LiteMemcached process.

set -x

LITE_ROOT=${LITE_ROOT:-/workspace/tests/Memcached/src}
LITE_CLI=""
for candidate in \
  "${LITE_ROOT}/lite-version-ascii/build/Lite/lite_cli" \
  "${LITE_ROOT}/lite-version-ascii-embedded/build/Lite/lite_cli"; do
  if [ -x "${candidate}" ]; then
    LITE_CLI=${candidate}
    break
  fi
done

if [ -z "${LITE_CLI}" ]; then
  echo "ERROR: lite_cli not found under ${LITE_ROOT}." 1>&2
  echo "       Build it first: ae_motivation_setup.sh build" 1>&2
  echo "       Killing memcached anyway would silently turn the LiteLib arm" 1>&2
  echo "       into the vanilla arm, so refusing to continue." 1>&2
  exit 1
fi

"${LITE_CLI}" -t /tmp/lite_memcached -p /tmp/memcached.sock -m 1 || {
  echo "ERROR: lite_cli failed to hand over to LiteMemcached" 1>&2
  exit 1
}

pgrep "memcached" | xargs kill -15
rm -rf /tmp/memcached.sock
