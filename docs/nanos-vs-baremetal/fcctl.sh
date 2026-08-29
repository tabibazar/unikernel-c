#!/bin/bash
# Boot the Linux control under the same Firecracker, same host, same 512 MiB.
set -u
cd ~/cmp
CFG=/tmp/fcctl.json
cat > "$CFG" <<J
{
  "boot-source": { "kernel_image_path": "$HOME/cmp/vmlinux",
                   "boot_args": "console=ttyS0 reboot=k panic=1 pci=off init=/init" },
  "machine-config": { "vcpu_count": 1, "mem_size_mib": 512 },
  "drives": [ { "drive_id": "rootfs", "path_on_host": "$HOME/cmp/ctl.ext4", "is_root_device": true, "is_read_only": false } ]
}
J
T0=$(date +%s.%N)
timeout 200 firecracker --no-api --config-file "$CFG" 2>&1 | python3 -u -c "
import sys,time
t0=float(sys.argv[1])
for l in sys.stdin:
    print('%.4f\t%s'%(time.time()-t0,l.rstrip('\n')),flush=True)
" "$T0" > "$1"
