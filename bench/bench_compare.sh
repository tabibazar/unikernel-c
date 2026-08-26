#!/bin/bash
# Controlled comparison: the same compute workload as a Linux process and as a
# BareMetal unikernel under Firecracker, on the same physical host.
#
# Reports, per trial: the guest's own elapsed time and the externally measured
# wall time. For the unikernel the difference between them is VM boot plus
# teardown, which is itself worth knowing.
set -u

WORK="${WORK:-160000000}"
TRIALS="${TRIALS:-3}"
cd "$HOME"

echo "### workload: $WORK candidates, $TRIALS trials each"
echo

echo "## A. Linux process (musl-free, host glibc, no VM)"
sed "s/^#define WORK   40000000ULL$/#define WORK   ${WORK}ULL/" bench.c > bench_sized.c
grep -m1 '^#define WORK' bench_sized.c
gcc -O2 -std=c99 -o bench_sized bench_sized.c
for i in $(seq 1 "$TRIALS"); do
    S=$(date +%s)
    OUT=$(./bench_sized | tail -1)
    E=$(date +%s)
    echo "trial $i: $OUT external_wall_s=$((E - S))"
done

echo
echo "## B. BareMetal unikernel under Firecracker (same host, 16 MiB, 1 vCPU)"
cd "$HOME/BareMetal-App"
cp "$HOME/bench_sized.c" bench_bm.c
sed -i "s/^MEMSIZE=[0-9]*/MEMSIZE=16/" baremetal.sh
grep -m1 '^MEMSIZE=' baremetal.sh
./1-build.sh bench_bm.c > /dev/null 2>&1 || { echo "build failed"; exit 1; }
ls -l baremetal.elf | awk '{print "image bytes:", $5}'

for i in $(seq 1 "$TRIALS"); do
    ./baremetal.sh stop >/dev/null 2>&1
    S=$(date +%s)
    ./baremetal.sh start >/dev/null 2>&1
    # Wait for the payload to finish rather than sleeping a fixed amount.
    for _ in $(seq 1 200); do
        if ./baremetal.sh output --full 2>/dev/null | grep -q BENCH_DONE; then break; fi
        sleep 2
    done
    E=$(date +%s)
    OUT=$(./baremetal.sh output --full 2>/dev/null | grep BENCH_DONE | tail -1)
    echo "trial $i: $OUT external_wall_s=$((E - S))"
    ./baremetal.sh stop >/dev/null 2>&1
done
