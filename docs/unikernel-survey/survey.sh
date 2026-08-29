#!/bin/bash
# One research run per project, three at a time.
#
# The research agent is stateless -- one question in, one answer out, nothing
# carried forward -- which is what makes this safe to fan out: no run can
# contaminate another, and a run that fails costs only its own project.
set -u

RESEARCH="$HOME/PycharmProjects/unikernel-c/build/research"
OUT=/tmp/survey/results
mkdir -p "$OUT"

export GEMINI_API_KEY=$(cat ~/.gemini_key)
export FIRECRAWL_API_KEY=$(cat ~/.firecrawl_key)

ask() {
    local name="$1" hint="$2"
    local slug=$(echo "$name" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-')
    [ -s "$OUT/$slug.txt" ] && { echo "  skip $name (already done)"; return; }

    "$RESEARCH" "Research the unikernel or library operating system project called '$name' ($hint). Find its actual source repository and report EXACTLY these fields, one per line, nothing before or after. Write 'unknown' for anything you cannot establish from a page you actually read - do not guess.
language: <main implementation language>
license: <e.g. BSD-3, Apache-2.0, MIT, GPL>
repo: <url of the source repository>
latest_release: <version and year, or 'none'>
last_commit: <approximate year-month of most recent commit>
hypervisors: <which it targets, e.g. QEMU/KVM, Xen, Firecracker, bare metal>
network_stack: <e.g. lwIP, custom, none>
tls: <yes/no/unknown, and via what library>
libc: <e.g. musl, newlib, custom, none>
status: <active, dormant, or archived - judge from the last commit date>" \
        > "$OUT/$slug.txt" 2>"$OUT/$slug.err"
    echo "  done $name"
}

# Sixteen came from the agent's own enumeration pass. Four are seeded: BareMetal
# (the subject of this project), Solo5 and Gramine (widely used, missed by the
# search), and HermitOS (the current name of what the search found as
# HermitCore). Seeding is recorded here so the method stays honest.
run_batch() {
    ask "MirageOS"      "OCaml unikernel, mirage.io" &
    ask "Unikraft"      "unikraft.org" &
    ask "Nanos"         "nanos.org, NanoVMs" &
    wait
    ask "OSv"           "osv.io, Cloudius Systems" &
    ask "IncludeOS"     "includeos.org, C++ unikernel" &
    ask "HermitOS"      "formerly HermitCore/RustyHermit, Rust unikernel" &
    wait
    ask "ToroKernel"    "torokernel, dedicated unikernel for microservices" &
    ask "Solo5"         "sandboxed execution base for unikernels" &
    ask "BareMetal"     "ReturnInfinity BareMetal exokernel, x86-64 assembly" &
    wait
    ask "runtime.js"    "JavaScript unikernel on V8" &
    ask "Rumprun"       "rumpkernel.org, NetBSD rump kernels" &
    ask "HaLVM"         "Haskell Lightweight Virtual Machine, Galois" &
    wait
    ask "LING"          "Erlang on Xen, erlangonxen.org" &
    ask "ClickOS"       "NEC ClickOS network function unikernel" &
    ask "Mini-OS"       "Xen Project MiniOS reference unikernel" &
    wait
    ask "Drawbridge"    "Microsoft Research library OS" &
    ask "Clive"         "Clive operating system, lsub.org" &
    ask "UniK"          "emc-advanced-dev unik, unikernel build and deploy tool" &
    wait
    ask "Gramine"       "Gramine library OS, formerly Graphene, SGX" &
    ask "Unikernel Linux" "UKL, Boston University, Linux as a unikernel" &
    wait
}

echo "=== starting $(date -u +%H:%M:%S) ==="
run_batch
echo "=== finished $(date -u +%H:%M:%S) ==="
echo "results: $(ls -1 $OUT/*.txt 2>/dev/null | wc -l) files"
