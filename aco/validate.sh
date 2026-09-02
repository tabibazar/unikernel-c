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
    make -s INSTANCE="$name" >/dev/null
    echo "=== $name (${secs}s x ${seeds} seeds) ==="
    for seed in $(seq 1 "$seeds"); do
        ./build/aco-"$name" --seconds "$secs" --seed "$seed" | grep ACO_DONE
    done
done
