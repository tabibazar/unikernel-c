#!/bin/bash
# Smallest memory each platform will run the same program in.
#
# Not a benchmark -- a capability boundary. The workload is deliberately tiny
# (200k iterations, no allocation, no I/O) so that what fails is the runtime
# refusing to boot, not the program running out of room.
set -u
cd ~/cmp; . ./harness.sh
export PATH="$HOME/.ops/bin:$PATH"

works() {   # works <platform> <mem>
    local log=/tmp/minram_$1_$2.log
    rm -f "$log"; : > "$log"
    case "$1" in
      nanos)
        ( run_nanos "$HOME/.ops/images/scfast_o2" "$log" "$2" & ) >/dev/null 2>&1 ;;
      baremetal)
        ( ./fcrun.sh "$HOME/cmp/scfast_bm.elf" "$2" "$log" & ) >/dev/null 2>&1 ;;
    esac
    local ok=1
    for _ in $(seq 1 40); do grep -q SELFCHECK_DONE "$log" 2>/dev/null && { ok=0; break; }; sleep 1; done
    pkill -f "qemu-system-x86_64.*scfast" 2>/dev/null
    pkill -f "firecracker --no-api" 2>/dev/null
    sleep 0.5
    return $ok
}

for plat in baremetal nanos; do
    echo "=== $plat ==="
    # Walk down the list rather than bisecting: the failure mode is not
    # guaranteed monotonic, and each probe is only a few seconds.
    for m in 512 256 128 64 32 24 16 12 8 6 4 3 2; do
        if works "$plat" "$m"; then echo "  ${m} MiB: ok"; last=$m
        else echo "  ${m} MiB: FAILED"; break; fi
    done
    echo "  -> smallest working: ${last:-none} MiB"
    unset last
done
