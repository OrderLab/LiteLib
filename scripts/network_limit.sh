#!/bin/bash
#
# Apply the control-network rate limits used by all LiteLib experiments.
#
# The shared CloudLab control network (${LITELIB_CTRL_IFACE}) is capped so that
# background traffic cannot perturb the measurements, while the dedicated
# experiment network (10.10.1.0/24) is left untouched.
#
# NOTE: `tc` and `iptables` state is *not* persistent -- re-run this script
# (or init.sh, which calls it) after every reboot.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

# === Configuration ===
read -r -a NODES <<<"${LITELIB_NODES}"
MYNAME=$(hostname -s)
SSH_USER="${LITELIB_SSH_USER}"
SSH_KEY="${LITELIB_SSH_KEY}"
# Never prompt for a host-key fingerprint: this script runs unattended.
SSH_OPTS=(-o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=3)

# === Parse CLI Arguments ===
IFACE="${1:-${LITELIB_CTRL_IFACE}}"
PKTS_PER_SEC="${2:-${LITELIB_PKTS_PER_SEC}}"
RATE_LIMIT="${3:-${LITELIB_RATE_LIMIT}}"

# === Function: Print current state ===
print_state() {
  echo "----------------------------------------"
  echo "🔎 TC qdisc:"
  tc qdisc show dev "$IFACE"
  echo ""

  echo "🔎 TC class:"
  tc class show dev "$IFACE"
  echo ""

  echo "🔎 TC filter:"
  tc filter show dev "$IFACE"
  echo ""

  echo "🔎 iptables INPUT chain:"
  iptables -L INPUT -v -n --line-numbers
  echo ""

  echo "🔎 iptables OUTPUT chain:"
  iptables -L OUTPUT -v -n --line-numbers
  echo "----------------------------------------"
  echo ""
}

# === Function: Block other node IPs ===
block_other_node_ips() {
  echo "🚫 Blocking non-10.* IPs of other nodes..."
  for NODE in "${NODES[@]}"; do
    if [ "$NODE" == "$MYNAME" ]; then continue; fi
    if [[ "$MYNAME" == "$NODE"* ]]; then continue; fi
    echo "🔍 Getting IPs from $NODE..."
    
    # Get IPs and ensure proper handling of the output
    IPS=$(ssh "${SSH_OPTS[@]}" -i "$SSH_KEY" ${SSH_USER}@$NODE "hostname -I" 2>/dev/null)
    if [ $? -eq 0 ] && [ ! -z "$IPS" ]; then
      echo "📡 Found IPs: $IPS"
      for IP in $IPS; do
        if [[ ! "$IP" == 10.* ]]; then
          echo "⛔ Blocking $IP"
          # First remove any existing rule for this IP
          iptables -D OUTPUT -d "$IP" -j REJECT 2>/dev/null || true
          # Then insert at the beginning of the chain
          iptables -I OUTPUT 1 -d "$IP" -j REJECT
        else
          echo "✅ Allowing $IP (10.* network)"
        fi
      done
    else
      echo "⚠️  Could not get IPs from $NODE"
    fi
  done
  echo "✅ Peer node non-10.* IPs blocked"
  echo ""
}

# === Function: Set outgoing packet rate limit ===
set_packet_rate_limit() {
  echo "📤 Setting OUTPUT packet rate limit..."

  # First remove any existing rules
  iptables -D OUTPUT -o lo -j ACCEPT 2>/dev/null || true
  iptables -D OUTPUT -d 10.0.0.0/8 -j ACCEPT 2>/dev/null || true
  iptables -D OUTPUT -d 172.16.0.0/12 -j ACCEPT 2>/dev/null || true
  iptables -D OUTPUT -d 192.168.0.0/16 -j ACCEPT 2>/dev/null || true
  iptables -D OUTPUT -d 0.0.0.0/0 -m hashlimit --hashlimit "$PKTS_PER_SEC"/sec --hashlimit-burst "$PKTS_PER_SEC" --hashlimit-mode dstip --hashlimit-name limit_pub_pkts -j ACCEPT 2>/dev/null || true
  iptables -D OUTPUT -d 0.0.0.0/0 -j REJECT 2>/dev/null || true

  # Then insert rules in correct order
  iptables -A OUTPUT -o lo -j ACCEPT
  iptables -A OUTPUT -d 10.0.0.0/8 -j ACCEPT
  iptables -A OUTPUT -d 172.16.0.0/12 -j ACCEPT
  iptables -A OUTPUT -d 192.168.0.0/16 -j ACCEPT
  iptables -A OUTPUT -d 0.0.0.0/0 -m hashlimit \
    --hashlimit "$PKTS_PER_SEC"/sec \
    --hashlimit-burst "$PKTS_PER_SEC" \
    --hashlimit-mode dstip \
    --hashlimit-name limit_pub_pkts \
    -j ACCEPT
  iptables -A OUTPUT -d 0.0.0.0/0 -j REJECT

  echo "✅ OUTPUT packet rate limited to $PKTS_PER_SEC pkts/sec"
  echo ""
}

# === Function: Set incoming packet rate limit ===
set_input_packet_rate_limit() {
  echo "📥 Setting INPUT packet rate limit..."

  iptables -C INPUT -i lo -j ACCEPT 2>/dev/null || iptables -A INPUT -i lo -j ACCEPT
  iptables -C INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT 2>/dev/null || \
    iptables -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT

  iptables -C INPUT -s 10.0.0.0/8 -j ACCEPT 2>/dev/null || iptables -A INPUT -s 10.0.0.0/8 -j ACCEPT
  iptables -C INPUT -s 172.16.0.0/12 -j ACCEPT 2>/dev/null || iptables -A INPUT -s 172.16.0.0/12 -j ACCEPT
  iptables -C INPUT -s 192.168.0.0/16 -j ACCEPT 2>/dev/null || iptables -A INPUT -s 192.168.0.0/16 -j ACCEPT

  iptables -C INPUT -m hashlimit \
    --hashlimit "$PKTS_PER_SEC"/sec \
    --hashlimit-burst "$PKTS_PER_SEC" \
    --hashlimit-mode srcip \
    --hashlimit-name limit_input_pkts \
    -j ACCEPT 2>/dev/null || \
  iptables -A INPUT -m hashlimit \
    --hashlimit "$PKTS_PER_SEC"/sec \
    --hashlimit-burst "$PKTS_PER_SEC" \
    --hashlimit-mode srcip \
    --hashlimit-name limit_input_pkts \
    -j ACCEPT

  iptables -C INPUT -j DROP 2>/dev/null || iptables -A INPUT -j DROP

  echo "✅ INPUT packet rate limited to $PKTS_PER_SEC pkts/sec"
  echo ""
}

# === Function: Set bandwidth rate limit via tc ===
set_bandwidth_limit() {
  echo "🔧 Setting egress bandwidth limit on $IFACE to $RATE_LIMIT..."

  # Delete any existing qdisc
  tc qdisc del dev "$IFACE" root 2>/dev/null || true

  # Add qdisc with default class ID (can be anything — we're parsing it later)
  tc qdisc add dev "$IFACE" root handle 1: htb default 30 r2q 100

  # Get default class hex value (like 0x30) and strip the 0x prefix
  DEFAULT_HEX=$(tc qdisc show dev "$IFACE" | awk '/htb/ && /default/ { for (i=1;i<=NF;i++) if ($i=="default") print $(i+1) }' | sed 's/^0x//')

  # Convert to decimal
  DEFAULT_CLASS_ID=$((16#$DEFAULT_HEX))

  # Add HTB class with the actual default ID
  tc class add dev "$IFACE" parent 1: classid 1:$DEFAULT_CLASS_ID htb rate "$RATE_LIMIT" quantum 1500

  # Direct all IPv4 traffic to default class
  tc filter add dev "$IFACE" protocol ip parent 1:0 prio 1 u32 match u32 0 0 flowid 1:$DEFAULT_CLASS_ID

  echo "✅ Bandwidth limit applied using default class 1:$DEFAULT_CLASS_ID"
  echo ""
}

# === Main ===
echo "📡 Interface: $IFACE"
echo "📈 Packet Limit: $PKTS_PER_SEC pkts/sec"
echo "🚦 Bandwidth Limit: $RATE_LIMIT"
echo ""

echo "===== BEFORE ====="
print_state

block_other_node_ips
set_packet_rate_limit
set_input_packet_rate_limit
set_bandwidth_limit

echo "===== AFTER ====="
print_state
