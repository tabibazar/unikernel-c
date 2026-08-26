#!/bin/bash
# Boot a nested Ubuntu VM (L2) that has a TUN driver, so Firecracker can run
# inside it with real tap networking — which this host kernel cannot provide.
#
# L0 = boxd's host, L1 = this machine (no tun, no modules), L2 = this Ubuntu
# guest (normal distro kernel, has tun), L3 = the BareMetal unikernel under
# Firecracker inside L2.
set -u
cd "$HOME" || exit 1

cat > user-data <<'EOF'
#cloud-config
password: bm
chpasswd: { expire: false }
ssh_pwauth: true
packages: [iproute2, iptables, dnsmasq-base, curl]
EOF
echo "instance-id: bm1
local-hostname: bm1" > meta-data

cloud-localds seed.img user-data meta-data

nohup setsid qemu-system-x86_64 \
    -M q35 -cpu host -enable-kvm -smp 2 -m 4096 \
    -drive file=noble.img,format=qcow2,if=virtio \
    -drive file=seed.img,format=raw,if=virtio \
    -netdev user,id=n0,hostfwd=tcp::2222-:22 \
    -device virtio-net-pci,netdev=n0 \
    -nographic > /tmp/l2.log 2>&1 < /dev/null &

echo "L2 booting, pid $!"
sleep 5
echo "--- early log ---"
tail -5 /tmp/l2.log
