#!/bin/bash
# R1: one colony, one host. Gap to optimum, iteration rate, and same-seed
# divergence. Wall-clock is the comparison basis -- iteration counts are not
# comparable across substrates.
#
# Fixed --iters (not --seconds) so the work is identical between the two runs
# of a seed: any difference in the result is then the platform, not the clock.
set -eu
cd "$(dirname "$0")"
HERE=$(pwd)
cd ../../aco
OUT="$HERE/results.tsv"
printf 'instance\tseed\trun\tbest\toptimum\tgap_bp\titers\tms\n' > "$OUT"
for inst in kroA100 pcb442; do
    [ -f "instances/$inst.h" ] || continue
    make -s INSTANCE="$inst" >/dev/null
    for seed in 1 2 3 4 5; do
        for run in 1 2; do
            line=$(./build/aco-"$inst" --iters 500 --seed "$seed" | grep ACO_DONE)
            get() { sed -n "s/.*$1=\(-\?[0-9]*\).*/\1/p" <<<"$line"; }
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                   "$inst" "$seed" "$run" "$(get best)" "$(get optimum)" \
                   "$(get gap_bp)" "$(get iters)" "$(get ms)" >> "$OUT"
        done
    done
done
echo "--- same-seed divergence ---"
awk -F'\t' 'NR>1 {k=$1"/"$2; if (k in seen) { pairs++; if (seen[k] != $4) diff++ } \
     else seen[k]=$4} END {printf "pairs=%d divergent=%d rate=%.4f\n", \
     pairs, diff, pairs ? diff/pairs : 0}' "$OUT"
