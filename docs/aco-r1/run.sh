#!/bin/bash
# R1: one colony, one host. Gap to optimum, iteration rate, and same-seed
# divergence. Wall-clock is the comparison basis -- iteration counts are not
# comparable across substrates.
#
# Fixed --iters (not --seconds) so the work is identical between the two runs
# of a seed: any difference in the result is then the platform, not the clock.
#
# Fields are pulled with awk, not sed: the ACO_DONE line is key=value pairs and
# BSD sed has no \? operator, so a GNU-flavoured regex silently matches nothing
# and every field comes back empty -- which reads as "no divergence" rather
# than as a broken script.
set -eu
cd "$(dirname "$0")"
HERE=$(pwd)
cd ../../aco
OUT="$HERE/results.tsv"

field() { awk -v k="$1" '{for(i=1;i<=NF;i++){split($i,a,"=");if(a[1]==k)print a[2]}}'; }

printf 'instance\tseed\trun\tbest\toptimum\tgap_bp\titers\tms\n' > "$OUT"
for inst in kroA100 pcb442; do
    [ -f "instances/$inst.h" ] || continue
    make -s INSTANCE="$inst" DIST_CACHE=1 LOCAL_SEARCH=1 >/dev/null
    for seed in 1 2 3 4 5; do
        for run in 1 2; do
            line=$(./build/aco-"$inst"-c1-l1 --iters 500 --seed "$seed" | grep ACO_DONE)
            best=$(field best    <<<"$line")
            opt=$(field optimum  <<<"$line")
            gap=$(field gap_bp   <<<"$line")
            its=$(field iters    <<<"$line")
            ms=$(field ms        <<<"$line")
            [ -n "$best" ] || { echo "FATAL: could not parse: $line" >&2; exit 1; }
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                   "$inst" "$seed" "$run" "$best" "$opt" "$gap" "$its" "$ms" >> "$OUT"
        done
    done
done
echo "--- same-seed divergence ---"
awk -F'\t' 'NR>1 {k=$1"/"$2; if (k in seen) { pairs++; if (seen[k] != $4) diff++ } \
     else seen[k]=$4} END {printf "pairs=%d divergent=%d rate=%.4f\n", \
     pairs, diff+0, pairs ? (diff+0)/pairs : 0}' "$OUT"
