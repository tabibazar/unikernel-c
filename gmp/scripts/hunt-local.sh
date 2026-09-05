#!/bin/bash
# hunt-local.sh -- run the chain hunt on an ordinary host, in resumable chunks.
#
# This is the "start small" path: it needs no metal host and no cloud, because
# both stages are ordinary userland programs. It exists so the search can be
# accumulating real progress while the capacity question is open, and so that
# the harness is proven before it is deployed rather than after.
#
# RESUMABILITY IS THE POINT
#
# The state is a single integer -- the next multiplier to examine -- kept in
# $STATE. A chunk that completes advances it; a chunk killed halfway does not,
# so the worst case is repeating one chunk. That is the same contract the cloud
# workers will have, which is why it is worth getting right here first: a
# scheduler that cannot lose work is much easier to reason about than one that
# checkpoints inside a chunk.
#
# Chunk size trades sieve amortisation against how much work a kill can lose.
# Sieving to depth 1e9 enumerates ~5.8e7 primes at a cost that is fixed rather
# than per-multiplier (measured: 200k multipliers 38.0s, 2M multipliers 39.2s),
# so chunks below about 2e6 waste most of their time re-enumerating primes.
#
#   ./hunt-local.sh            # run until stopped
#   ./hunt-local.sh --status   # where it has got to
#
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$REPO/results/hunt-local}"
STATE="$OUT/next_m"
HITS="$OUT/hits.txt"
LOG="$OUT/progress.log"

K="${K:-6}"
PRIMORIAL="${PRIMORIAL:-2357}"
J="${J:-1}"
DEPTH="${DEPTH:-1000000000}"
CHUNK="${CHUNK:-2000000}"
MIN_REPORT="${MIN_REPORT:-3}"

mkdir -p "$OUT"
[ -f "$STATE" ] || echo 1 > "$STATE"

if [ "${1:-}" = "--status" ]; then
	next=$(cat "$STATE")
	echo "next multiplier : $next"
	echo "examined so far : $((next - 1))"
	echo "hits recorded   : $(grep -c HUNT_HIT "$HITS" 2>/dev/null || echo 0)"
	[ -f "$LOG" ] && { echo "last chunks:"; tail -5 "$LOG"; }
	exit 0
fi

# Built once here rather than per chunk; on a Mac GMP lives under the Homebrew
# prefix, which is not on the default search path.
GMPDIR="$(brew --prefix gmp 2>/dev/null || echo /usr)"
CC_SIEVE="$OUT/cc_sieve"
CC_HUNT="$OUT/cc_hunt"
for prog in cc_sieve cc_hunt; do
	src="$REPO/$prog.c"; bin="$OUT/$prog"
	if [ ! -x "$bin" ] || [ "$src" -nt "$bin" ]; then
		echo "building $prog"
		gcc -O2 -o "$bin" "$src" -I"$GMPDIR/include" -L"$GMPDIR/lib" -lgmp -lm \
			|| { echo "build of $prog failed" >&2; exit 1; }
	fi
done

# Refuse to run a search whose stages have not just passed their own
# known-answer checks. A hunt that silently sieves wrongly would look exactly
# like a hunt that is simply unlucky, for as long as it ran.
"$CC_SIEVE" --selftest >/dev/null 2>&1 || { echo "cc_sieve selftest FAILED -- not running" >&2; exit 1; }
"$CC_HUNT"  --selftest >/dev/null 2>&1 || { echo "cc_hunt selftest FAILED -- not running"  >&2; exit 1; }
echo "selftests passed; starting from $(cat "$STATE")"

trap 'echo "stopping after current chunk"; STOP=1' INT TERM
STOP=0

while [ "$STOP" -eq 0 ]; do
	m=$(cat "$STATE")
	t0=$(date +%s)

	out=$("$CC_SIEVE" --k "$K" --primorial "$PRIMORIAL" --j "$J" \
	                  --m-start "$m" --m-count "$CHUNK" --depth "$DEPTH" \
	                  --list --quiet 2>/dev/null \
	      | "$CC_HUNT" --k "$K" --primorial "$PRIMORIAL" --j "$J" \
	                   --min-report "$MIN_REPORT" --quiet 2>/dev/null)
	rc=$?

	if [ $rc -ne 0 ]; then
		echo "chunk from $m failed (rc=$rc) -- not advancing" | tee -a "$LOG"
		sleep 5
		continue
	fi

	# Any hit line is kept verbatim. These are candidates, not records: a
	# full-length hit still needs re-testing and then a primality proof
	# before it is claimed anywhere.
	echo "$out" | grep '^HUNT_HIT' >> "$HITS" 2>/dev/null

	done_line=$(echo "$out" | grep '^HUNT_DONE')
	hist_line=$(echo "$out" | grep '^HUNT_HIST')
	printf '%s m=[%s,%s) %s | %s | %ds\n' \
		"$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$m" "$((m + CHUNK))" \
		"$done_line" "$hist_line" "$(( $(date +%s) - t0 ))" >> "$LOG"

	echo $((m + CHUNK)) > "$STATE"
done
