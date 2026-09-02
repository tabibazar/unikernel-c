#!/bin/bash
# The engine is correct only if it reaches known optima. Unit tests cannot
# establish that an optimiser optimises. Ten seeds each, because one proves
# nothing. Binaries are per-instance: a shared name lets make reuse the
# previous instance's build and misattribute every result.
set -eu
cd "$(dirname "$0")"
CACHE="${CACHE:-1}"
LS="${LS:-1}"
echo "config: DIST_CACHE=$CACHE LOCAL_SEARCH=$LS"
for spec in berlin52:20:10 kroA100:30:10 pcb442:60:10 rat783:60:5; do
    IFS=: read -r name secs seeds <<< "$spec"
    [ -f "instances/$name.h" ] || { echo "skip $name (no header)"; continue; }
    # Every compile-time setting is pinned here and asserted below, so the
    # committed table always names the configuration that produced it.
    make -s INSTANCE="$name" DIST_CACHE="$CACHE" LOCAL_SEARCH="$LS" >/dev/null
    echo "=== $name (${secs}s x ${seeds} seeds) ==="
    for seed in $(seq 1 "$seeds"); do
        out=$(./build/aco-"$name"-c"$CACHE"-l"$LS" --seconds "$secs" --seed "$seed")
        # Assert the binary really is the pinned configuration, rather than
        # trusting that make rebuilt it.
        grep -q "cache=$CACHE ls=$LS" <<<"$out" || { echo "FATAL: wrong build config" >&2; exit 1; }
        grep ACO_DONE <<<"$out"
    done
done
