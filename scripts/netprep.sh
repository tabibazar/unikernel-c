#!/bin/bash
# br0 + tap0 for the BareMetal guest, NAT mode only.
#
# BareMetal-Firecracker's own mkbr0.sh takes a different branch on a wired NIC:
# it enslaves eth0 to the bridge and moves eth0's IP across. On a cloud VM whose
# only link is eth0, that drops the connection we are driving this through. The
# NAT branch (which that script reserves for Wi-Fi hosts) gives the guest
# outbound connectivity without touching eth0 at all — which is all the hunter
# needs to reach api.telegram.org.
set -u

BRIDGE=br0
TAP=tap0
BRIDGE_IP=172.19.0.1/24
HOST_IFACE=$(ip route get 1.1.1.1 2>/dev/null | awk '{print $5; exit}')

echo "host iface: $HOST_IFACE (untouched)"

# Idempotent: tear down anything left from a previous attempt.
sudo ip link del "$TAP" 2>/dev/null || true
sudo ip link del "$BRIDGE" 2>/dev/null || true

sudo ip link add name "$BRIDGE" type bridge
sudo ip addr add "$BRIDGE_IP" dev "$BRIDGE"
sudo ip link set "$BRIDGE" up

sudo sysctl -q net.ipv4.ip_forward=1
sudo iptables -t nat -C POSTROUTING -o "$HOST_IFACE" -j MASQUERADE 2>/dev/null || \
    sudo iptables -t nat -A POSTROUTING -o "$HOST_IFACE" -j MASQUERADE
sudo iptables -C FORWARD -i "$BRIDGE" -o "$HOST_IFACE" -j ACCEPT 2>/dev/null || \
    sudo iptables -A FORWARD -i "$BRIDGE" -o "$HOST_IFACE" -j ACCEPT
sudo iptables -C FORWARD -i "$HOST_IFACE" -o "$BRIDGE" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
    sudo iptables -A FORWARD -i "$HOST_IFACE" -o "$BRIDGE" -m state --state RELATED,ESTABLISHED -j ACCEPT

# DHCP for the guest. --port=0 means DHCP only, no DNS listener.
sudo pkill -f "dnsmasq.*$BRIDGE" 2>/dev/null || true
sudo dnsmasq --port=0 --interface="$BRIDGE" --bind-interfaces \
     --dhcp-range=172.19.0.10,172.19.0.254,12h \
     --dhcp-option=option:router,172.19.0.1 \
     --dhcp-option=option:dns-server,1.1.1.1,8.8.8.8 \
     --pid-file=/var/run/dnsmasq-"$BRIDGE".pid

sudo ip tuntap add dev "$TAP" mode tap
sudo ip link set "$TAP" promisc on
sudo ip link set "$TAP" master "$BRIDGE"
sudo ip link set "$TAP" up

echo "--- result ---"
ip -brief addr show "$BRIDGE"
ip -brief addr show "$TAP"
echo "still reachable: $(ip -4 addr show "$HOST_IFACE" | awk '/inet /{print $2}')"
