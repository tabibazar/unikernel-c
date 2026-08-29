#!/bin/bash
# Run the whole matrix. Five runs of everything: bmhunt has two shared vCPUs,
# so a single timing off this box is noise.
set -u
cd ~/cmp; . ./harness.sh
export PATH="$HOME/.ops/bin:$PATH"
N=${N:-5}

wait_for() {   # wait_for <log> <marker> <secs>
    for _ in $(seq 1 "$3"); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 1; done
    return 1
}

one() {   # one <platform> <test> <opt> <run>
    local plat="$1" test="$2" opt="$3" run="$4"
    local marker log t0 t1
    case "$test" in selfcheck) marker=SELFCHECK_DONE ;; bench) marker=BENCH_DONE ;; esac
    log=/tmp/run_${plat}_${test}_${opt}_${run}.log
    rm -f "$log"; : > "$log"
    t0=$(date +%s.%N)
    case "$plat" in
      linux)
          local T0; T0=$(date +%s.%N)
          ./${test}_${opt} 2>&1 | stamp "$T0" > "$log" ;;
      nanos)
          ( run_nanos "$HOME/.ops/images/${test}_${opt}" "$log" 512 & ) >/dev/null 2>&1
          wait_for "$log" "$marker" 280
          pkill -f "qemu-system-x86_64.*${test}_${opt}" 2>/dev/null; sleep 0.5 ;;
      baremetal)
          ( ./fcrun.sh "$HOME/cmp/${test}_bm.elf" 512 "$log" & ) >/dev/null 2>&1
          wait_for "$log" "$marker" 280
          pkill -f "firecracker --no-api" 2>/dev/null; sleep 0.5 ;;
    esac
    t1=$(date +%s.%N)
    record "$plat" "$test" "$opt" "$run" 512 "$(echo "$t1 - $t0" | bc)" "$log"
}

for test in selfcheck bench; do
  for run in $(seq 1 "$N"); do
    one linux     "$test" o0 "$run"
    one linux     "$test" o2 "$run"
    one nanos     "$test" o0 "$run"
    one nanos     "$test" o2 "$run"
    one baremetal "$test" bm "$run"
  done
done
echo "=== done ==="
column -t -s $'\t' "$TSV"
