#!/usr/bin/env bash

set -e
set -x

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  echo "ERROR: Please run with sudo/root."
  exit 1
fi

echo "[1/4] Installing chrony..."
export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y chrony socat

CONF="/etc/chrony/chrony.conf"
BACKUP="/etc/chrony/chrony.conf.bak.$(date +%s)"

echo "[2/4] Backing up $CONF -> $BACKUP"
cp -a "$CONF" "$BACKUP"

echo "[3/4] Writing a simple chrony config..."
cat > "$CONF" <<'EOF'
# Minimal chrony config for LAN experiments (ms-level sync or better)
# Uses public NTP sources; replace/add local stratum servers if you have them.

server time.google.com iburst
server time.cloudflare.com iburst

# Step the clock if the offset is >1s during the first 3 updates (avoid long slews)
makestep 1.0 3

# Keep RTC in sync (mostly irrelevant on cloud nodes but harmless)
rtcsync
EOF

echo "[4/4] Restarting chrony..."
systemctl enable chrony >/dev/null 2>&1 || true
systemctl restart chrony

echo
echo "=== chrony status ==="
chronyc tracking || true
echo
chronyc sources -v || true

echo
echo "Done. If 'Last offset' is well under 1 ms on both hosts, you're in good shape."
