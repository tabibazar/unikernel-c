#!/bin/bash
# A 30-minute measured run.
#
# The work is real: it fills the two weakest columns of the unikernel survey
# (TLS support, and network stack / libc), which the first pass left mostly
# unknown. The point of the exercise, though, is the accounting -- every unit
# records its wall time, model calls, tokens in and out, and search and fetch
# counts, so that a two-week projection rests on measurements rather than
# guesses.
set -u

R="$HOME/PycharmProjects/unikernel-c/build/research"
OUT=/tmp/survey/long
LOG=/tmp/survey/long/units.tsv
mkdir -p "$OUT"
export GEMINI_API_KEY=$(cat ~/.gemini_key) FIRECRAWL_API_KEY=$(cat ~/.firecrawl_key)

[ -f "$LOG" ] || printf "unit\tseconds\tllm_calls\ttok_in\ttok_out\tsearches\tfetches\tsteps\toutcome\n" > "$LOG"

unit() {
    local slug="$1" question="$2"
    [ -s "$OUT/$slug.txt" ] && return
    local t0=$(date +%s)
    "$R" "$question" > "$OUT/$slug.txt" 2>"$OUT/$slug.err"
    local t1=$(date +%s)

    local cost=$(grep -m1 '^COST' "$OUT/$slug.txt" 2>/dev/null)
    local outcome="answered"
    grep -q "NO ANSWER" "$OUT/$slug.txt" 2>/dev/null && outcome="no_answer"

    local lc=$(echo "$cost" | sed -n 's/.*llm_calls=\([0-9]*\).*/\1/p')
    local ti=$(echo "$cost" | sed -n 's/.*tokens_in=\([0-9]*\).*/\1/p')
    local to=$(echo "$cost" | sed -n 's/.*tokens_out=\([0-9]*\).*/\1/p')
    local se=$(echo "$cost" | sed -n 's/.*searches=\([0-9]*\).*/\1/p')
    local fe=$(echo "$cost" | sed -n 's/.*fetches=\([0-9]*\).*/\1/p')
    local st=$(echo "$cost" | sed -n 's/.*steps=\([0-9]*\).*/\1/p')

    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
        "$slug" "$((t1-t0))" "${lc:-0}" "${ti:-0}" "${to:-0}" "${se:-0}" "${fe:-0}" "${st:-0}" "$outcome" >> "$LOG"
    echo "  $slug  ${outcome}  $((t1-t0))s  ${ti:-0} in / ${to:-0} out"
}

PROJECTS="MirageOS Unikraft Nanos OSv IncludeOS HermitOS ToroKernel Solo5 BareMetal Rumprun Gramine LING ClickOS Mini-OS HaLVM runtime.js UniK Drawbridge Clive Unikernel-Linux"

echo "=== started $(date -u +%H:%M:%S) ==="

# Two units per project, run two at a time. Concurrency stays low on purpose:
# three at once is what tripped the search API's rate limiter last time.
for p in $PROJECTS; do
    slug=$(echo "$p" | tr 'A-Z.' 'a-z-' | tr -cd 'a-z0-9-')
    unit "${slug}-tls" "Does the unikernel or library OS project '$p' support TLS, and if so via which library (for example mbedTLS, OpenSSL, BearSSL, or its own)? Answer only from pages you actually read, and say clearly if no source states it." &
    unit "${slug}-stack" "For the unikernel or library OS project '$p': what network stack does it use (for example lwIP, a custom stack, or none), and what libc does it provide (for example musl, newlib, NetBSD libc, or its own)? Answer only from pages you actually read." &
    wait
done

echo "=== finished $(date -u +%H:%M:%S) ==="
echo "units: $(($(wc -l < "$LOG") - 1))"
