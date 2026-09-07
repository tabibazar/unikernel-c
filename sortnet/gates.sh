#!/bin/bash
# gates.sh -- run the validation gates from the study design.
#
# The gates are exact published integers, so they are pass/fail rather than
# judgement: the optimal size for n=4 is 5 comparators and for n=6 is 12, and a
# search that cannot rediscover those has no business being pointed at n=18
# where the answer is unknown.
#
# The gate is the MEDIAN of 10 seeds, not the best. A single lucky seed proves
# nothing, and reporting the best of ten is the oldest way to make a search look
# better than it is.
#
#   ./gates.sh          # G1 and G2
#   ./gates.sh G3       # add the n=8 gate
set -uo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${BIN:-/tmp/sn_gp}"
SEEDS="${SEEDS:-10}"

gcc -O2 -o "$BIN" "$DIR/gp.c" || { echo "build failed" >&2; exit 1; }

run_gate() {
	local gate=$1 n=$2 len=$3 budget=$4
	echo "=== $gate: n=$n, target $len comparators, budget $budget, $SEEDS seeds ==="
	local results=() solved=0
	for s in $(seq 1 "$SEEDS"); do
		out=$("$BIN" --n "$n" --len "$len" --seed "$s" --budget "$budget" --quiet 2>/dev/null)
		if grep -q GP_SOLVED <<<"$out"; then
			ev=$(grep -oE 'evals=[0-9]+' <<<"$out" | cut -d= -f2)
			results+=("$ev"); solved=$((solved+1))
			printf "  seed %-3s solved at %'d evaluations\n" "$s" "$ev"
		else
			# A failed seed is recorded as the full budget, not dropped. Dropping
			# it would compute the median over successes only and quietly
			# flatter a search that fails half the time.
			results+=("$budget")
			printf "  seed %-3s FAILED within budget\n" "$s"
		fi
	done
	local sorted median
	sorted=$(printf '%s\n' "${results[@]}" | sort -n)
	median=$(printf '%s\n' "$sorted" | awk '{a[NR]=$1} END{print (NR%2)?a[(NR+1)/2]:int((a[NR/2]+a[NR/2+1])/2)}')
	printf "  -> solved %d/%d, median %'d evaluations of %'d budget  " "$solved" "$SEEDS" "$median" "$budget"
	if [ "$solved" -eq "$SEEDS" ] && [ "$median" -lt "$budget" ]; then echo "PASS"; else echo "FAIL"; fi
	echo
}

run_gate G1 4 5  100000
run_gate G2 6 12 1000000
if [ "${1:-}" = "G3" ]; then run_gate G3 8 19 10000000; fi
