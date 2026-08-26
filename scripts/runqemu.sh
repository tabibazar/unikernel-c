#!/bin/bash
# Boot the BareMetal unikernel under QEMU's microvm machine type.
#
# Why not Firecracker here: this host kernel has no TUN/TAP driver and no
# loadable modules, so no tap device can exist, and Firecracker only does
# networking through a tap. QEMU's microvm uses the same virtio-mmio layout
# Firecracker does, and its user-mode networking is a userspace TCP stack —
# no tap, no bridge, no privileges. Device order matches baremetal.sh: net
# first, then the root drive.
set -u
cd "$HOME/BareMetal-App" || exit 1

SECS=${1:-120}

timeout "${SECS}" qemu-system-x86_64 \
    -M microvm,x-option-roms=off,pit=off,pic=off,isa-serial=on,rtc=on \
    -enable-kvm -cpu host -m 512 -smp 1 \
    -kernel baremetal.elf -append "" \
    -nodefaults -no-reboot -serial stdio \
    -netdev user,id=net0 -device virtio-net-device,netdev=net0 \
    -drive id=root,file=disk.img,format=raw,if=none \
    -device virtio-blk-device,drive=root 2>&1

echo "--- qemu exited: $? (124 = hit the ${SECS}s cap, which means it kept running) ---"
