#!/bin/bash
# Boot a BareMetal unikernel under Firecracker -- the same VMM BareMetal Cloud
# runs -- and stamp every serial line with seconds since the firecracker process
# was execed. No network interface: this host has no TUN driver, and none of the
# compute tests touch the network.
#   fcrun.sh <kernel.elf> <mem_mib> <logfile>
set -u
K="$1"; MEM="$2"; LOG="$3"
CFG=$(mktemp /tmp/fc-XXXX.json)
cat > "$CFG" <<J
{
  "boot-source": { "kernel_image_path": "$K", "boot_args": "" },
  "machine-config": { "vcpu_count": 1, "mem_size_mib": $MEM },
  "drives": [ { "drive_id": "rootfs", "path_on_host": "$HOME/BareMetal-App/BareMetal-Firecracker/disk.img", "is_root_device": true, "is_read_only": false } ]
}
J
T0=$(date +%s.%N)
timeout 300 firecracker --no-api --config-file "$CFG" 2>&1 | python3 -u -c "
import sys,time
t0=float(sys.argv[1])
for l in sys.stdin:
    print('%.4f\t%s'%(time.time()-t0,l.rstrip('\n')),flush=True)
" "$T0" > "$LOG"
rm -f "$CFG"
