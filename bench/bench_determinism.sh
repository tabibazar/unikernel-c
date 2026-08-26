#!/bin/bash
# Does the BareMetal unikernel compute the same answer twice?
#
# The workload is fixed arithmetic over a fixed range, so every pass must return
# identical counts. Three passes inside a single boot removes the harness, the
# API, the serial capture and VM scheduling from the question: if the counts
# differ here, the guest itself is not computing deterministically.
#
# Also varies MEMSIZE, to test whether 16 MiB is simply too tight.
set -u

WORK="${WORK:-40000000}"
cd "$HOME"

echo "### fixed range, 3 passes per boot, WORK=$WORK"
echo

sed -e "s/^#define WORK   40000000ULL$/#define WORK   ${WORK}ULL/" \
    -e "s/^#define REPEAT 1$/#define REPEAT 3/" bench.c > bench_det.c
grep -m1 '^#define WORK' bench_det.c; grep -m1 '^#define REPEAT' bench_det.c

echo
echo "## A. Linux process, -O2 (control: must be identical three times)"
gcc -O2 -std=c99 -o bench_det bench_det.c && ./bench_det | grep BENCH_DONE

echo
echo "## B. Linux process, -O0 (matches the BareMetal build's optimisation level)"
gcc -O0 -std=c99 -o bench_det_o0 bench_det.c && ./bench_det_o0 | grep BENCH_DONE

cd "$HOME/BareMetal-App"
cp "$HOME/bench_det.c" bench_det_bm.c

for MEM in 16 256; do
    echo
    echo "## C. BareMetal unikernel under Firecracker, MEMSIZE=${MEM}"
    sed -i "s/^MEMSIZE=[0-9]*/MEMSIZE=${MEM}/" baremetal.sh
    ./1-build.sh bench_det_bm.c > /dev/null 2>&1 || { echo "build failed"; continue; }
    ./baremetal.sh stop >/dev/null 2>&1
    ./baremetal.sh start >/dev/null 2>&1
    for _ in $(seq 1 300); do
        if [ "$(./baremetal.sh output --full 2>/dev/null | grep -c BENCH_DONE)" -ge 3 ]; then break; fi
        sleep 2
    done
    ./baremetal.sh output --full 2>/dev/null | grep BENCH_DONE
    ./baremetal.sh stop >/dev/null 2>&1
done
