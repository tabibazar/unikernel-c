#!/bin/bash
set -uo pipefail

# Drive inbound_bench and find out what kind of limit the platform has.
#
# The guest counts what it served; this side counts what it got back, and the
# two disagreeing is itself information -- a request the client thinks failed
# but the guest served is a different fault from one that never arrived.
#
# Three phases, in this order for a reason:
#
#   1 CHURN      one connection per request. The condition webserver.c creates
#                and the one the ~350 claim was presumably measured under.
#   2 RECOVERY   after churn stops, wait, then try again. If serving resumes,
#                the limit is a reclaimable resource and not a cap. This is
#                the phase that decides whether request-serving workloads are
#                viable here at all, and it is the one nobody has run.
#   3 KEEPALIVE  many requests down one connection. If churn is the mechanism,
#                this should sail past whatever number phase 1 found.
#
# Usage:  ./drive.sh <host> [port]

HOST="${1:?usage: drive.sh <host> [port]}"
PORT="${2:-8080}"
OUT="${OUT:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/results}"
N="${N:-1200}"            # requests to attempt in the churn phase
RECOVER_WAIT="${RECOVER_WAIT:-180}"   # > 2*MSL, so TIME_WAIT has drained
mkdir -p "$OUT"

command -v curl >/dev/null || { echo "needs curl" >&2; exit 1; }

# Every reply from inbound_bench carries "served=" in its body. Checking for
# it is not paranoia: the first run of this harness reported 40/40 successes
# and "retires the ~350 claim" while the benchmark had actually failed to bind
# (EADDRINUSE) and curl was cheerfully talking to an unrelated service on the
# same port. A load generator that does not verify what answered is not
# measuring the thing it names.
hit() {  # -> 1 on a verified benchmark reply, 0 otherwise
	curl -s --max-time 5 --connect-timeout 3 \
	     -H 'Connection: close' "http://$HOST:$PORT/" 2>/dev/null \
	  | grep -q '^served=' && echo 1 || echo 0
}

echo "=== identifying the listener ==="
probe=$(curl -s --max-time 5 --connect-timeout 3 "http://$HOST:$PORT/" 2>/dev/null || true)
case "$probe" in
	served=*) echo "  ok: inbound_bench is answering on $HOST:$PORT" ;;
	"")       echo "  nothing answered on $HOST:$PORT -- is the guest up?" >&2; exit 1 ;;
	*)        echo "  something else is on $HOST:$PORT, not inbound_bench:" >&2
	          printf "    %.80s\n" "$probe" >&2
	          echo "  refusing to measure it." >&2; exit 1 ;;
esac

echo "=== phase 1: churn, one connection per request, up to $N ==="
ok=0; fail=0; first_fail=-1
for i in $(seq 1 "$N"); do
	if [ "$(hit)" = "1" ]; then
		ok=$((ok+1))
	else
		fail=$((fail+1))
		[ "$first_fail" -lt 0 ] && { first_fail=$i; echo "  first client-side failure at request $i"; }
		# Three consecutive failures is enough; hammering a dead listener
		# only measures curl's timeout.
		[ "$fail" -ge 3 ] && [ $((i - first_fail)) -le 5 ] && break
	fi
	[ $((i % 50)) -eq 0 ] && echo "  $i attempted, $ok ok, $fail failed"
done
echo "CHURN_RESULT attempted=$i ok=$ok failed=$fail first_fail_at=$first_fail" | tee "$OUT/churn.txt"

if [ "$first_fail" -lt 0 ]; then
	echo
	echo "No failure in $N churned requests."
	echo "That alone retires the ~350 claim for this configuration -- record it"
	echo "and raise N if you want to find the real ceiling."
	exit 0
fi

echo
echo "=== phase 2: recovery -- waiting ${RECOVER_WAIT}s, then trying again ==="
echo "(longer than 2*MSL, so any TIME_WAIT pool will have drained)"
sleep "$RECOVER_WAIT"
r_ok=0
for i in $(seq 1 20); do [ "$(hit)" = "1" ] && r_ok=$((r_ok+1)); done
echo "RECOVERY_RESULT ok=$r_ok of 20" | tee "$OUT/recovery.txt"
if [ "$r_ok" -gt 0 ]; then
	echo "  -> RECLAIMABLE. Serving resumed after a pause, so the ceiling is a"
	echo "     resource that drains, not a hard cap. A keep-alive or pooled"
	echo "     client would likely never reach it."
else
	echo "  -> HARD. Nothing served after the wait; the listener did not come"
	echo "     back on its own. Request-serving workloads are out until this"
	echo "     is fixed in the port."
fi

echo
echo "=== phase 3: keep-alive, many requests down one connection ==="
# curl reuses one connection across URLs given in a single invocation, so this
# is the cheapest honest keep-alive test available without writing a client.
urls=""; for i in $(seq 1 500); do urls="$urls http://$HOST:$PORT/"; done
ka=$(curl -s -o /dev/null --max-time 120 -w '%{num_connects} %{http_code}' $urls 2>/dev/null || echo "ERR")
echo "KEEPALIVE_RESULT $ka  (num_connects should stay ~1 if reuse worked)" | tee "$OUT/keepalive.txt"

echo
echo "results in $OUT -- pair them with the guest's BENCH_STAT lines from the"
echo "serial console, which say how many the server thinks it served."
