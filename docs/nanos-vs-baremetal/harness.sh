#!/bin/bash
# Same host, same hypervisor, same source: Nanos vs BareMetal vs Linux.
#
# Everything below runs on one machine (bmhunt: AMD EPYC, 2 vCPU, KVM). Both
# guests get -smp 1, the same memory, the same user-mode network and a serial
# console piped through a timestamper, so every line carries milliseconds since
# the QEMU process was execed. Comparing across hosts measures hosts.
set -u
export PATH="$HOME/.ops/bin:$PATH" OPS_DIR="$HOME/.ops"
cd ~/cmp

MEM=${MEM:-512}
TSV=${TSV:-$HOME/cmp/results.tsv}
[ -f "$TSV" ] || printf "platform\ttest\topt\trun\tmem_mib\twall_s\tfirst_out_s\tstart_s\tdone_s\tresult\n" > "$TSV"

# Prefix each line with seconds since a T0 captured just before exec.
stamp() { python3 -u -c "
import sys,time
t0=float(sys.argv[1])
for l in sys.stdin:
    print('%.4f\t%s'%(time.time()-t0,l.rstrip('\n')),flush=True)
" "$1"; }

run_nanos() {   # run_nanos <image> <logfile> <mem>
    local T0; T0=$(date +%s.%N)
    timeout 300 qemu-system-x86_64 \
        -machine q35,accel=kvm -cpu host -m "${3}" -smp 1 \
        -device virtio-scsi-pci,id=scsi0 -device scsi-hd,drive=hd0 \
        -drive file="$1",format=raw,if=none,id=hd0 \
        -netdev user,id=n0 -device virtio-net,netdev=n0 \
        -vga none -display none -device isa-debug-exit -serial stdio \
        2>&1 | stamp "$T0" > "$2"
}

run_baremetal() {   # run_baremetal <elf> <logfile> <mem>
    local T0; T0=$(date +%s.%N)
    timeout 300 qemu-system-x86_64 \
        -M microvm,x-option-roms=off,pit=off,pic=off,isa-serial=on,rtc=on \
        -enable-kvm -cpu host -m "${3}" -smp 1 \
        -kernel "$1" -append "" -nodefaults -no-reboot -serial stdio \
        -netdev user,id=net0 -device virtio-net-device,netdev=net0 \
        -drive id=root,file="$HOME/BareMetal-App/disk.img",format=raw,if=none \
        -device virtio-blk-device,drive=root \
        2>&1 | stamp "$T0" > "$2"
}

# Pull the marker timings out of a stamped log and append a row.
record() {   # record <platform> <test> <opt> <run> <mem> <wall> <log>
    local log="$7"
    local first done_line start_line
    first=$(head -1 "$log" | cut -f1)
    start_line=$(grep -m1 -E '(SELFCHECK|BENCH)_START' "$log" | cut -f1)
    done_line=$(grep -m1 -E '(SELFCHECK|BENCH)_DONE' "$log" | cut -f1)
    local result
    result=$(grep -m1 -E '(SELFCHECK|BENCH)_DONE' "$log" | cut -f2- | tr '\t' ' ')
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
        "$1" "$2" "$3" "$4" "$5" "$6" "${first:-NA}" "${start_line:-NA}" \
        "${done_line:-NA}" "${result:-NO_RESULT}" >> "$TSV"
    echo "  $1/$2/$3 run$4: wall=${6}s first_out=${first:-NA} done=${done_line:-NA}"
}
