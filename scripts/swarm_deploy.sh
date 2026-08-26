#!/bin/bash
# Build and deploy N Cunningham-chain workers to BareMetal Cloud.
#
# BareMetal has no environment, so each worker's identity is compiled in: the
# source is copied N times, its WORKER_ID #define rewritten, and each copy built
# into its own unikernel image. Rebuilds take about a second, so N images is
# cheap.
#
#   BM_API_KEY=... bash swarm_deploy.sh [swarm_size]
set -eu

: "${BM_API_KEY:?set BM_API_KEY first}"
N="${1:-3}"
FLOOR="${REPORT_FLOOR:-9}"
# Candidates are p = 6k - 1, so resuming from a point on the number line means
# dividing by 6. Default picks up where swarm run 01 reached (~1.12e10).
START="${START_K:-1866666666}"

cd "$HOME/BareMetal-App"
TG=$(cat "$HOME/.tg_token")
CHAT="${TELEGRAM_CHAT_ID:?set TELEGRAM_CHAT_ID}"

for i in $(seq 0 $((N - 1))); do
    echo "=== worker $i of $N ==="
    cp "$HOME/cunningham.c" cunningham_w${i}.c
    sed -i "s|PUT_BOT_TOKEN_HERE|${TG}|; s|PUT_CHAT_ID_HERE|${CHAT}|" cunningham_w${i}.c
    sed -i "s|^#define WORKER_ID          0$|#define WORKER_ID          ${i}|" cunningham_w${i}.c
    sed -i "s|^#define WORKER_COUNT       1$|#define WORKER_COUNT       ${N}|" cunningham_w${i}.c
    sed -i "s|^#define REPORT_FLOOR       9 |#define REPORT_FLOOR       ${FLOOR} |" cunningham_w${i}.c
    sed -i "s|^#define START_K            1ULL |#define START_K            ${START}ULL |" cunningham_w${i}.c

    grep -E "^#define (WORKER_ID|WORKER_COUNT|REPORT_FLOOR|START_K)" cunningham_w${i}.c | sed 's/  */ /g'

    ./1-build.sh cunningham_w${i}.c > /dev/null 2>&1
    IMAGE_ID=$(./bm-api.sh images upload "cunningham-w${i}" baremetal.elf | awk -F': ' '/^id:/{print $2}')
    INSTANCE_ID=$(./bm-api.sh instances create "cunningham-w${i}" 1 16 "$IMAGE_ID" | awk -F': ' '/^id:/{print $2}')
    echo "worker $i -> image $IMAGE_ID, instance $INSTANCE_ID"
done

echo "=== swarm ==="
./bm-api.sh instances list
