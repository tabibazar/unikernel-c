#!/bin/bash
# The engine is correct only if it reaches known optima. Unit tests cannot
# establish that an optimiser optimises. Ten seeds each, because one proves
# nothing. Binaries are per-instance: a shared name lets make reuse the
# previous instance's build and misattribute every result.
set -eu
cd "$(dirname "$0")"
for spec in berlin52:20:10 kroA100:30:10 pcb442:60:10 rat783:60:5; do
    IFS=: read -r name secs seeds <<< "$spec"
    [ -f "instances/$name.h" ] || { echo "skip $name (no header)"; continue; }
    # Pinned to the setting the committed table was measured under. The
    # default is now 1; leaving this unpinned would give ~2x the iterations
    # in the same wall-clock and quietly drift the numbers away from the
    # write-up. (It does not change the verdict: 2x iterations moved
    # pcb442 from 15.45% to 14.49%.)
    make -s INSTANCE="$name" DIST_CACHE=0 >/dev/null
    echo "=== $name (${secs}s x ${seeds} seeds) ==="
    for seed in $(seq 1 "$seeds"); do
        out=$(./build/aco-"$name"-c0 --seconds "$secs" --seed "$seed")
        # Assert the binary really is the pinned configuration, rather than
        # trusting that make rebuilt it.
        grep -q "cache=0" <<<"$out" || { echo "FATAL: wrong cache setting" >&2; exit 1; }
        grep ACO_DONE <<<"$out"
    done
done
