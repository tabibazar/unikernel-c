#!/bin/bash
# Poll the swarm until every worker has passed a target position, then mark done.
#
# Tracks the MINIMUM position across workers, not the maximum: the range is only
# fully covered up to the slowest worker. Writes a progress line each poll so the
# run has a record afterwards.
set -u

TARGET="${TARGET:-100000000000}"
INTERVAL="${INTERVAL:-300}"
cd "$HOME/BareMetal-App" || exit 1
export BM_API_KEY=$(cat "$HOME/.bm_key")

rm -f /tmp/swarm_done
echo "$(date -Is) watching for min position >= $TARGET" > /tmp/swarm_progress.log

while true; do
    MIN=""
    for w in 0 1 2; do
        ID=$(./bm-api.sh instances list 2>/dev/null | awk "/cunningham-w$w/{print \$1}")
        [ -z "$ID" ] && continue
        V=$(./bm-api.sh instances logs "$ID" 2>/dev/null \
            | grep -o 'searched to [0-9]*' | tail -1 | grep -o '[0-9]*')
        [ -z "$V" ] && continue
        if [ -z "$MIN" ] || [ "$V" -lt "$MIN" ]; then MIN=$V; fi
    done

    if [ -z "$MIN" ]; then
        echo "$(date -Is) no workers reachable" >> /tmp/swarm_progress.log
        sleep "$INTERVAL"; continue
    fi

    PCT=$(( MIN * 100 / TARGET ))
    echo "$(date -Is) min=$MIN (${PCT}% of target)" >> /tmp/swarm_progress.log

    if [ "$MIN" -ge "$TARGET" ]; then
        echo "$(date -Is) TARGET REACHED at $MIN" >> /tmp/swarm_progress.log
        touch /tmp/swarm_done
        break
    fi
    sleep "$INTERVAL"
done
