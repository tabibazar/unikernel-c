#!/bin/bash
set -euo pipefail

# Run probe.py on BareMetal and on Linux, on the same machine, and diff them.
#
# The comparison is the whole design. A probe that fails on both is a probe
# bug; a probe that passes on Linux and fails on BareMetal is a finding about
# the port. Running only the BareMetal side would leave every failure ambiguous.
#
# Needs a bare-metal x86-64 Linux host: Firecracker wants KVM, and a laptop
# with nested virtualisation disabled will get as far as building and no
# further.
#
#   ./run-metal.sh

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BMAPP="${BMAPP:-$HOME/BareMetal-App}"
OUT="${OUT:-$REPO/pyprobe/results}"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-600}"

[ "$(uname -m)" = "x86_64" ] || { echo "needs x86-64" >&2; exit 1; }
[ -d "$BMAPP" ] || { echo "no BareMetal-App at $BMAPP -- clone and ./setup.sh first" >&2; exit 1; }
[ -e /dev/kvm ] && [ -r /dev/kvm ] || { echo "/dev/kvm missing or unreadable -- needs a metal host" >&2; exit 1; }
[ -f "$BMAPP/BareMetal-AppPort/python.app" ] || { echo "python.app missing -- run $BMAPP/setup.sh" >&2; exit 1; }

mkdir -p "$OUT"

echo "=== host ==="
{ uname -srm; awk -F: '/model name/{print $2; exit}' /proc/cpuinfo; } | tee "$OUT/host.txt"

# --- Linux control -----------------------------------------------------------
# Uses the SAME CPython version the port vendors, built from the same tarball,
# so a difference cannot be blamed on the interpreter version.
PYSRC="$BMAPP/BareMetal-AppPort/build/Python-3.14.7"
if [ -x "$OUT/cpython-linux/bin/python3" ]; then
	CTRL="$OUT/cpython-linux/bin/python3"
else
	echo "=== building the Linux control interpreter (same source) ==="
	( cd "$PYSRC" && ./configure --prefix="$OUT/cpython-linux" -q >/dev/null 2>&1 \
	  && make -j"$(nproc)" -s >/dev/null 2>&1 && make install -s >/dev/null 2>&1 ) \
	  || { echo "control build failed; falling back to system python3" >&2; }
	CTRL="$OUT/cpython-linux/bin/python3"
	[ -x "$CTRL" ] || CTRL="$(command -v python3)"
fi
echo "control interpreter: $CTRL ($("$CTRL" -V 2>&1))"

echo "=== Linux control run ==="
"$CTRL" "$REPO/pyprobe/probe.py" 2>&1 | tee "$OUT/linux.txt"

# --- BareMetal ---------------------------------------------------------------
# 1-build.sh with a .py argument installs the curated stdlib plus the script as
# /pylib/main.py onto disk.img, then builds the unikernel around the prebuilt
# python.app. It does not recompile the interpreter.
echo "=== building the unikernel around probe.py ==="
cp "$REPO/pyprobe/probe.py" "$BMAPP/"
( cd "$BMAPP" && ./1-build.sh probe.py 2>&1 | tail -5 )

# baremetal.sh deletes its Firecracker log path and then passes it as
# --log-path; Firecracker opens without creating, so the VM never starts.
# Reported upstream from this repo already -- patched here so a fresh clone
# does not silently waste the run.
if grep -q 'rm -f "$FCLOG"' "$BMAPP/baremetal.sh" && ! grep -q 'touch "$FCLOG"' "$BMAPP/baremetal.sh"; then
	sed -i 's|^\(\s*\)rm -f "$FCLOG"|\1rm -f "$FCLOG"\n\1touch "$FCLOG"|' "$BMAPP/baremetal.sh"
	echo "patched baremetal.sh --log-path bug"
fi

VMLOG=$(grep -E '^VMLOG=' "$BMAPP/baremetal.sh" | cut -d= -f2 | tr -d '"'"'"'')
VMLOG="${VMLOG:-/tmp/fc-vm.log}"

echo "=== booting ==="
START=$(date +%s.%N)
( cd "$BMAPP" && nohup ./baremetal.sh start > /tmp/bm-start.log 2>&1 < /dev/null & )

# BareMetal cannot time itself reliably, so cold start is taken from outside:
# wall clock from launching the VM to the interpreter's first printed line.
FIRST=""
for _ in $(seq 1 "$BOOT_TIMEOUT"); do
	if [ -s "$VMLOG" ] && grep -q PROBE_START "$VMLOG" 2>/dev/null; then
		FIRST=$(date +%s.%N); break
	fi
	sleep 1
done
if [ -n "$FIRST" ]; then
	echo "COLD_START_TO_FIRST_PYTHON_LINE $(awk -v a="$START" -v b="$FIRST" 'BEGIN{printf "%.3f", b-a}') s" | tee "$OUT/coldstart.txt"
else
	echo "never reached PROBE_START within ${BOOT_TIMEOUT}s" | tee "$OUT/coldstart.txt"
fi

# Let it finish; the blocking network probes are last and may sit on a timeout.
for _ in $(seq 1 "$BOOT_TIMEOUT"); do
	grep -q PROBE_DONE "$VMLOG" 2>/dev/null && break
	sleep 1
done
cp "$VMLOG" "$OUT/baremetal.txt" 2>/dev/null || true
( cd "$BMAPP" && ./baremetal.sh stop >/dev/null 2>&1 ) || true

# --- diff --------------------------------------------------------------------
echo
echo "=== what differs ==="
python3 - "$OUT/linux.txt" "$OUT/baremetal.txt" <<'PY'
import sys, re
def load(p):
    d = {}
    try: text = open(p, errors="replace").read()
    except OSError: return d
    for line in text.splitlines():
        m = re.match(r"PROBE (\w+)\s+(\S+)\s+(\S+)\s*(.*)", line)
        if m: d[m.group(3)] = (m.group(1), m.group(4))
    return d
lin, bm = load(sys.argv[1]), load(sys.argv[2])
if not bm:
    print("no BareMetal results parsed -- the guest produced no PROBE lines")
    sys.exit(0)
names = sorted(set(lin) | set(bm))
print(f"{'probe':<34}{'linux':<8}{'baremetal':<11}note")
regressions = missing = 0
for n in names:
    ls = lin.get(n, ("ABSENT", ""))[0]
    bs, bd = bm.get(n, ("ABSENT", "did not run -- guest stopped before this probe"))
    if ls == bs: continue
    note = bd[:70]
    if ls == "PASS" and bs in ("FAIL", "ABSENT"):
        regressions += 1
        if bs == "ABSENT": missing += 1
    print(f"{n:<34}{ls:<8}{bs:<11}{note}")
print()
print(f"probes passing on Linux but not on BareMetal: {regressions}"
      f" (of which {missing} never ran)")
print("XFAIL on BareMetal = failed exactly as the port's docs predict.")
PY
echo
echo "results in $OUT"
