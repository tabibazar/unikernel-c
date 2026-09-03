#!/bin/bash
set -euo pipefail

# Paired before/after capture across the two platform bugs Return Infinity is
# fixing: the interrupt-path register clobber, and the 3.7x compute penalty.
#
# Run it ONCE before their fix ships and ONCE after, on the SAME machine:
#
#     ./scripts/before_after.sh before
#     ...their fix lands...
#     ./scripts/before_after.sh after
#
# The before-run is unrepeatable. Once the fix is deployed there is no way back
# to the faulty platform, and a fix confirmed only by "the bad thing stopped
# happening" is much weaker evidence than a paired measurement on one host with
# one set of binaries. docs/arithmetic-fault/ has a 2026-08 figure, but it was
# taken on a different machine, so it cannot carry the comparison alone.
#
# Four measurements, chosen so each bug has both a direct probe and a
# workload-level consequence:
#
#   faultscope   the fault rate itself, mismatches per 200M operations.
#                This is the number in the bug report: 127 / 2e8 = 6.35e-7.
#   selfcheck    the same fault seen through a plain modular-exponentiation
#                loop -- the shape a real workload meets it in.
#   bench        fixed-work timing, 64-bit modular arithmetic. This is the
#                workload the 3.7x was measured on.
#   prp_bench    fixed-work timing, GMP big-integer. NEVER MEASURED on this
#                platform. Every budget figure in docs/portfolio/ applies the
#                3.7x to big-integer work by assumption; this is the run that
#                turns that into a number, and taking it before the fix is the
#                only way to attribute the change afterwards.
#
# Each is run on BareMetal and as a Linux process on the same host, because a
# ratio between two platforms on one machine is a platform measurement and a
# number from one platform alone is not.

LABEL="${1:?usage: before_after.sh <before|after>}"
case "$LABEL" in before|after) ;; *) echo "label must be 'before' or 'after'" >&2; exit 2;; esac

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BMAPP="${BMAPP:-$HOME/BareMetal-App}"
OUT="${OUT:-$REPO/docs/arithmetic-fault/paired-$LABEL.txt}"
FCLOG="${FCLOG:-/tmp/firecracker.log}"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-900}"

[ "$(uname -m)" = "x86_64" ] || { echo "needs x86-64" >&2; exit 1; }
[ -d "$BMAPP" ] || { echo "no BareMetal-App at $BMAPP" >&2; exit 1; }
[ -e /dev/kvm ] && [ -r /dev/kvm ] || { echo "/dev/kvm missing or unreadable -- needs a metal host" >&2; exit 1; }
command -v firecracker >/dev/null || { echo "firecracker not on PATH" >&2; exit 1; }

exec > >(tee "$OUT") 2>&1

echo "PAIRED_RUN label=$LABEL date=$(date -Is)"
echo "host: $(uname -srm)"
echo "cpu:  $(awk -F: '/model name/{print $2; exit}' /proc/cpuinfo | sed 's/^ *//')"
echo "baremetal-app: $(cd "$BMAPP" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo

# baremetal.sh deletes $FCLOG and then passes it as --log-path; Firecracker
# opens without creating, so the VM never starts. Upstream bug, already
# reported from this repo -- pre-created here so a stale checkout cannot waste
# the one run that cannot be repeated.
touch "$FCLOG"

run_bm() {   # run_bm <source.c> <label>
	local src="$1" name="$2"
	echo "--- BareMetal: $name ---"
	cp "$src" "$BMAPP/"
	( cd "$BMAPP" && ./1-build.sh "$(basename "$src")" >/dev/null 2>&1 )
	timeout "$BOOT_TIMEOUT" "$BMAPP/baremetal.sh" start 2>&1 || true
	echo
}

run_linux() {  # run_linux <source.c> <label> [extra cflags]
	local src="$1" name="$2"; shift 2
	echo "--- Linux control: $name ---"
	gcc -O2 -o "/tmp/$name" "$src" "$@"
	"/tmp/$name"
	echo
}

echo "=== 1. faultscope -- the fault rate itself ==="
run_bm    "$REPO/docs/arithmetic-fault/faultscope.c" faultscope
run_linux "$REPO/docs/arithmetic-fault/faultscope.c" faultscope

echo "=== 2. selfcheck -- the fault through a workload loop ==="
run_bm    "$REPO/bench/selfcheck.c" selfcheck
run_linux "$REPO/bench/selfcheck.c" selfcheck

echo "=== 3. bench -- fixed work, 64-bit modular arithmetic ==="
echo "(this is the workload the 3.7x figure was measured on)"
run_bm    "$REPO/bench/bench.c" bench
run_linux "$REPO/bench/bench.c" bench

echo "=== 4. prp_bench -- fixed work, GMP big integer ==="
echo "(never measured on this platform; see gmp/scripts/run-metal.sh)"
if [ -x "$REPO/gmp/scripts/run-metal.sh" ]; then
	"$REPO/gmp/scripts/run-metal.sh" || echo "prp_bench leg failed -- see its own output"
else
	echo "SKIPPED: gmp/scripts/run-metal.sh not executable"
fi

echo
echo "PAIRED_DONE label=$LABEL -> $OUT"
echo
echo "After both runs exist, the comparison to report is:"
echo "  fault rate   before vs after  (expect non-zero -> zero)"
echo "  bench ratio  BareMetal/Linux, before vs after"
echo "  prp ratio    BareMetal/Linux, before vs after  <- the new one"
