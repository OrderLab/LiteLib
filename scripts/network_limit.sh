#!/bin/bash

# === Configuration ===
NODES=(node0 node1 node2 node3)
MYNAME=$(hostname)

# === Parse CLI Arguments ===
IFACE="${1:-eno1}"
PKTS_PER_SEC="${2:-10000}"
RATE_LIMIT="${3:-100mbit}"

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
  echo "🚫 Blocking IPs of other nodes..."
  for NODE in "${NODES[@]}"; do
    if [ "$NODE" == "$MYNAME" ]; then continue; fi
    if [[ "$MYNAME" == "$NODE"* ]]; then continue; fi
    echo "🔍 Getting IPs from $NODE..."
    IPS=$(ssh -o ConnectTimeout=3 $NODE "hostname -I" 2>/dev/null)
    if [ -z "$IPS" ]; then
      echo "⚠️  Could not reach $NODE"
      continue
    fi
    for IP in $IPS; do
      echo "⛔ Blocking $IP"
      iptables -C OUTPUT -d "$IP" -j REJECT 2>/dev/null || iptables -A OUTPUT -d "$IP" -j REJECT
    done
  done
  echo "✅ Peer node IPs blocked"
  echo ""
}

# === Function: Set outgoing packet rate limit ===
set_packet_rate_limit() {
  echo "📤 Setting OUTPUT packet rate limit..."

  # Allow loopback and private networks
  iptables -C OUTPUT -o lo -j ACCEPT 2>/dev/null || iptables -A OUTPUT -o lo -j ACCEPT
  iptables -C OUTPUT -d 10.0.0.0/8 -j ACCEPT 2>/dev/null || iptables -A OUTPUT -d 10.0.0.0/8 -j ACCEPT
  iptables -C OUTPUT -d 172.16.0.0/12 -j ACCEPT 2>/dev/null || iptables -A OUTPUT -d 172.16.0.0/12 -j ACCEPT
  iptables -C OUTPUT -d 192.168.0.0/16 -j ACCEPT 2>/dev/null || iptables -A OUTPUT -d 192.168.0.0/16 -j ACCEPT

  # Rate limit public destinations
  iptables -C OUTPUT -d 0.0.0.0/0 -m hashlimit \
    --hashlimit "$PKTS_PER_SEC"/sec \
    --hashlimit-burst "$PKTS_PER_SEC" \
    --hashlimit-mode dstip \
    --hashlimit-name limit_pub_pkts \
    -j ACCEPT 2>/dev/null || \
  iptables -A OUTPUT -d 0.0.0.0/0 -m hashlimit \
    --hashlimit "$PKTS_PER_SEC"/sec \
    --hashlimit-burst "$PKTS_PER_SEC" \
    --hashlimit-mode dstip \
    --hashlimit-name limit_pub_pkts \
    -j ACCEPT

  iptables -C OUTPUT -d 0.0.0.0/0 -j REJECT 2>/dev/null || iptables -A OUTPUT -d 0.0.0.0/0 -j REJECT

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
