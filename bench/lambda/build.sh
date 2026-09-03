#!/bin/bash
set -euo pipefail

# Build a STATIC x86-64 prp_bench for AWS Lambda's provided.al2023 runtime.
#
# Static, not dynamic, for a specific reason: this repo's build container is
# Debian bookworm (glibc 2.36) and Lambda's provided.al2023 runs glibc 2.34.
# A dynamically linked binary built here fails at load there with a version
# error that reads like a missing file. Static linking removes the question.
#
# The workload is deliberately the SAME prp_bench.c the unikernel runs. It
# reports cycles per operation via RDTSC, which is what makes the comparison
# meaningful across substrates: cycles are independent of clock rate, so a
# Lambda number and a BareMetal number compare directly even though neither
# platform will tell us honestly what frequency it is running at.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT="${OUT:-$SCRIPT_DIR/build}"
GMP_SRC="${GMP_SRC:?set GMP_SRC to a configured gmp-6.3.0 tree (see gmp/scripts)}"
IMAGE="${IMAGE:-bmbuild:latest}"

mkdir -p "$OUT"

docker run --rm --platform linux/amd64 \
  -v "$REPO":/repo -v "$GMP_SRC":/gmp -v "$OUT":/out \
  -w /out "$IMAGE" bash -c '
set -eux
GMP_LIB=/gmp/.libs/libgmp.a
[ -f "$GMP_LIB" ] || GMP_LIB=/gmp/libgmp.a
gcc -O2 -m64 -static -I /gmp /repo/gmp/prp_bench.c "$GMP_LIB" -o /out/prp_bench
strip /out/prp_bench
file /out/prp_bench
/out/prp_bench 2357 3
'
echo "built: $OUT/prp_bench"
