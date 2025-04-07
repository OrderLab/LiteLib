#!/bin/bash

# === Config ===
IFACE=${IFACE:-eno1}
CLASSID_HEX=$(tc qdisc show dev "$IFACE" | awk '/htb/ && /default/ { for (i=1;i<=NF;i++) if ($i=="default") print $(i+1) }' | sed 's/^0x//')
CLASSID_DEC=$((16#$CLASSID_HEX))
CLASSID="1:$CLASSID_DEC"

# === Arguments ===
TARGET=${1}
RATE=${2:-150m}              # e.g., 150m or 1g
TARGET_USER=${3:-$USER}      # optional: ssh user (default = local user)

if [ -z "$TARGET" ]; then
  echo "❗ Usage: $0 <target-ip-or-host> <rate> [ssh-user]"
  exit 1
fi

echo "🚀 Starting remote nuttcp server on $TARGET via SSH..."
ssh -o ConnectTimeout=3 "$TARGET_USER@$TARGET" \
  "nuttcp -S" \
  || {
    echo "❌ Failed to start nuttcp on remote target"
    exit 1
  }

sleep 1  # give server a sec to start

echo "📤 Running test: sending to $TARGET at max $RATE for 10s..."
nuttcp -T10 -R$RATE "$TARGET"

echo ""
echo "📊 HTB Class Stats (dev=$IFACE, class=$CLASSID):"
tc -s class show dev "$IFACE" | grep -A10 "class htb $CLASSID"

# Optional cleanup: kill remote server
echo ""
read -p "🛑 Stop remote nuttcp server? [Y/n] " STOP
if [[ "$STOP" =~ ^[Yy]?$ ]]; then
  ssh "$TARGET_USER@$TARGET" "pkill -f 'nuttcp -S'"
  echo "✅ Remote nuttcp server stopped"
else
  echo "ℹ️  Server left running on $TARGET"
fi
