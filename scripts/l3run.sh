#!/bin/bash
# Drive L2 over SSH: install Firecracker, build tap networking, and run the
# BareMetal unikernel there with real outbound connectivity.
set -u

SSH="sshpass -p bm ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 ubuntu@127.0.0.1"
SCP="sshpass -p bm scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P 2222"

cd "$HOME/BareMetal-App" || exit 1

echo "=== copying unikernel into L2 ==="
$SSH 'mkdir -p ~/bm' || exit 1
$SCP baremetal.elf disk.img baremetal.sh ubuntu@127.0.0.1:~/bm/ || exit 1
$SCP ~/netprep.sh ubuntu@127.0.0.1:~/ || exit 1
$SSH 'ls -lh ~/bm/'

echo "=== installing firecracker in L2 ==="
$SSH 'command -v firecracker >/dev/null && { firecracker --version | head -1; exit 0; }
      REL=$(curl -fsSL https://api.github.com/repos/firecracker-microvm/firecracker/releases/latest | grep -o "\"tag_name\": \"[^\"]*" | cut -d\" -f4)
      cd /tmp && curl -fsSL "https://github.com/firecracker-microvm/firecracker/releases/download/${REL}/firecracker-${REL}-x86_64.tgz" -o fc.tgz
      tar xf fc.tgz && sudo install -m755 release-${REL}-x86_64/firecracker-${REL}-x86_64 /usr/local/bin/firecracker
      firecracker --version | head -1'

echo "=== building tap0 + NAT in L2 ==="
$SSH 'sudo apt-get install -y -qq dnsmasq-base >/dev/null 2>&1; sudo systemctl stop dnsmasq 2>/dev/null; bash ~/netprep.sh'

echo "=== more RAM for the guest (TLS needs headroom) ==="
$SSH 'sed -i "s/^MEMSIZE=32/MEMSIZE=256/" ~/bm/baremetal.sh && grep -m1 "^MEMSIZE=" ~/bm/baremetal.sh'

echo "=== starting the unikernel in L2 ==="
$SSH 'cd ~/bm && sudo ./baremetal.sh start 2>&1 | tail -3'
echo "--- letting it run 150s ---"
sleep 150
$SSH 'cd ~/bm && sudo ./baremetal.sh output --full 2>&1 | head -45'
$SSH 'cd ~/bm && sudo ./baremetal.sh stop >/dev/null 2>&1; echo "(stopped)"'
