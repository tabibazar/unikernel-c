#!/bin/bash
set -euo pipefail

# Measure milliseconds per Fermat PRP test on BareMetal, against a Linux
# control on the same machine.
#
# This is the one measurement docs/portfolio/CUNNINGHAM-SIZE-RECORDS.md is
# waiting on. It cannot be taken on a development Mac: Docker Desktop
# exposes no nested KVM, so Firecracker will not start. Run it on a bare
# metal Linux x86-64 host -- an AWS c5.metal is what the rest of this repo
# used.
#
# What it produces, and why both halves are needed:
#
#   Linux control    prp_bench built and run as an ordinary process
#   BareMetal        the identical source, the identical libgmp.a, as a
#                    unikernel under Firecracker
#
# The ratio between them is the answer. The 3.7x figure this repo carries
# was measured on 64-bit modular arithmetic (docs/nanos-vs-baremetal/), not
# on big-integer work, and applying it to GMP has been an assumption since
# the costing document was written. One machine, one source, one library --
# so the only variable left is the absence of an operating system.
#
# Timing is RDTSC, so the two halves compare as cycles and neither side has
# to know its own clock rate. clock() never advances here; see
# docs/aco-r1/BAREMETAL.md.

BOLD=$'\033[1m'; NORM=$'\033[0m'
say() { printf '%s==> %s%s\n' "$BOLD" "$*" "$NORM"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

REPO_GMP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APPPORT="${APPPORT:-$HOME/BareMetal-App/BareMetal-AppPort}"
BMAPP="${BMAPP:-$HOME/BareMetal-App}"
WORK="${WORK:-$HOME/gmp-metal}"
PRIMORIAL="${PRIMORIAL:-2357}"
REPS="${REPS:-20}"
RESULTS="$WORK/results.txt"

# ---------------------------------------------------------------- preflight

[ "$(uname -m)" = "x86_64" ] || die "must run on x86-64; this is $(uname -m)"
[ -d "$BMAPP" ] || die "no BareMetal-App at $BMAPP -- clone it and run ./setup.sh first"
[ -f "$APPPORT/build/musl-1.2.6/lib/libc.a" ] || \
	die "musl not built at $APPPORT -- run $BMAPP/setup.sh first (it takes a while)"
[ -e /dev/kvm ] || die "/dev/kvm missing -- this needs a bare metal host, not a nested VM"
[ -r /dev/kvm ] || die "/dev/kvm not readable by $(id -un) -- add yourself to the kvm group"
command -v firecracker >/dev/null || die "firecracker not on PATH"

mkdir -p "$WORK"
: > "$RESULTS"

record() { printf '%s\n' "$*" | tee -a "$RESULTS"; }

record "host: $(uname -srm)"
record "cpu:  $(awk -F: '/model name/{print $2; exit}' /proc/cpuinfo | sed 's/^ *//')"
record "date: $(date -Is)"
record ""

# ------------------------------------------------------------ build libgmp

say "building libgmp.a for the target"
export BUILD_DIR="$WORK/build"
export MUSL_INC="$APPPORT/build/musl-1.2.6/sysroot/usr/local/musl/include"
"$REPO_GMP/scripts/get-gmp.sh"
"$REPO_GMP/scripts/build-gmp.sh"

GMP_SRC="$BUILD_DIR/gmp-6.3.0"
GMP_LIB="$GMP_SRC/.libs/libgmp.a"
[ -f "$GMP_LIB" ] || GMP_LIB="$GMP_SRC/libgmp.a"

# ------------------------------------------------- correctness before speed

# A wrong answer produced quickly is not a result. This is the same
# known-answer set the development machine ran, re-run here because the
# library was rebuilt by a different compiler on different hardware.
say "known-answer selftest (hosted, this machine)"
gcc -O2 -m64 -mcmodel=large -I "$GMP_SRC" \
	"$REPO_GMP/gmp_selftest.c" "$GMP_LIB" -o "$WORK/selftest"
"$WORK/selftest" | tee -a "$RESULTS"
record ""

# ------------------------------------------------------------ Linux control

# Built against the SAME libgmp.a the unikernel links, so the comparison
# isolates the platform rather than the library.
say "Linux control"
gcc -O2 -m64 -mcmodel=large -DPRP_PRIMORIAL="$PRIMORIAL" -DPRP_REPS="$REPS" \
	-I "$GMP_SRC" "$REPO_GMP/prp_bench.c" "$GMP_LIB" -o "$WORK/prp_bench_linux"

LINUX_OUT="$("$WORK/prp_bench_linux")"
record "--- Linux control ---"
record "$LINUX_OUT"
record ""

# ---------------------------------------------------------------- unikernel

say "linking prp_bench.app"
export APPPORT GMP_BUILD="$GMP_SRC" OUT_DIR="$WORK/applink"
# The app takes no argv on this platform, so its parameters are baked in
# and must match the control's exactly or the comparison is meaningless.
CPPFLAGS="-DPRP_PRIMORIAL=$PRIMORIAL -DPRP_REPS=$REPS" \
	"$REPO_GMP/scripts/link-app.sh" "$REPO_GMP/prp_bench.c" | tail -20

say "wrapping into baremetal.elf"
cp "$OUT_DIR/prp_bench.app" "$BMAPP/BareMetal-Firecracker/sys/"
( cd "$BMAPP/BareMetal-Firecracker" && ./build.sh prp_bench.app )
cp "$BMAPP/BareMetal-Firecracker/sys/baremetal.elf" "$WORK/"

# baremetal.sh removes $FCLOG and then hands the same path to Firecracker
# as --log-path. Firecracker opens that file and does not create it, so it
# exits immediately with LoggerInitialization ... No such file or directory
# and the VM never runs. Reported upstream from this repo already
# (docs/aco-r1/BAREMETAL.md); pre-creating the file here so a stale
# checkout does not silently waste a metal session.
FCLOG="${FCLOG:-/tmp/firecracker.log}"
touch "$FCLOG"

say "booting"
record "--- BareMetal ---"

# BareMetal cannot be asked when it is done, and a hung guest must not hang
# the harness: the run is bounded and the console captured either way.
CONSOLE="$WORK/console.txt"
timeout "${BOOT_TIMEOUT:-300}" \
	"$BMAPP/baremetal.sh" start > "$CONSOLE" 2>&1 || true

if grep -q PRP_DONE "$CONSOLE"; then
	grep -E 'PRP_START|PRP_DONE|PRP_PROJECT' "$CONSOLE" | tee -a "$RESULTS"
else
	record "NO PRP_DONE LINE -- guest did not reach the end. Console follows:"
	tail -40 "$CONSOLE" | tee -a "$RESULTS"
	record ""
	record "Check first: does the app image fit? crt0 prints"
	record "  'crt0: app image needs ~X MiB ... only has Y MiB'"
	record "when the VM's RAM is smaller than the linked image."
fi
record ""

# ------------------------------------------------------------------ verdict

say "verdict"
lin=$(sed -n 's/.*ticks_per_test=\([0-9]*\).*/\1/p' <<<"$LINUX_OUT" | head -1)
bm=$(sed -n 's/.*ticks_per_test=\([0-9]*\).*/\1/p' "$CONSOLE" 2>/dev/null | head -1)

if [ -n "${lin:-}" ] && [ -n "${bm:-}" ]; then
	record "Linux      : $lin cycles/PRP"
	record "BareMetal  : $bm cycles/PRP"
	record "penalty    : $(awk -v a="$bm" -v b="$lin" 'BEGIN{printf "%.2fx", a/b}')"
	record ""
	# The bar the costing document set. Cycles are converted at the host's
	# nominal clock, which the operator should confirm -- a wrong clock
	# rate moves this verdict.
	GHZ="${GHZ:-3.0}"
	record "at ${GHZ} GHz that is $(awk -v c="$bm" -v g="$GHZ" 'BEGIN{printf "%.1f ms", c/(g*1e6)}') per PRP"
	record "  bar: under 49 ms fits a \$500 cap at 1x expectation"
	record "       under 16 ms fits at 3x (~95% chance of a find)"
	record ""
	record "core-hours for one expected k=6 chain (7.3e9 tests): $(awk -v c="$bm" -v g="$GHZ" 'BEGIN{printf "%.0f", 7.3e9*(c/(g*1e9))/3600}')"
else
	record "could not extract both figures -- see $CONSOLE"
fi

record ""
record "full results: $RESULTS"
