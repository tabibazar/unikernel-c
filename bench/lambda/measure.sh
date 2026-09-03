#!/bin/bash
set -euo pipefail

# Measure AWS Lambda as a substrate for fan-out compute, in the same units
# the BareMetal side reports: cycles per PRP test, and dollars per unit of
# work done.
#
# Three numbers, and they are not interchangeable:
#
#   Init Duration    what Lambda spent starting a NEW execution environment.
#                    This is the honest comparator for BareMetal's 31 ms boot
#                    -- and note Lambda does not pay it per invocation, which
#                    is the whole reason a naive "Firecracker Linux boots in
#                    950 ms" comparison is misleading.
#   Duration         wall time the handler ran.
#   Billed Duration  what you actually pay for. The only one that enters the
#                    cost column.
#
# Cold starts are forced by updating the function's environment, which
# retires every warm sandbox. Waiting for natural expiry takes minutes and is
# not reliable.

FN="${FN:-prp-bench}"
REGION="${REGION:-$(aws configure get region 2>/dev/null || echo us-east-1)}"
COLD_N="${COLD_N:-5}"
WARM_N="${WARM_N:-5}"

# Published on-demand x86 price per GB-second. NOT verified by this script --
# two prior research passes failed to establish current cloud pricing, so
# treat it as an input, override it if it is wrong, and note that the
# comparison's ranking is far less sensitive to it than the absolute figures.
PRICE_GB_SEC="${PRICE_GB_SEC:-0.0000166667}"
PRICE_REQ="${PRICE_REQ:-0.0000002}"

MEM=$(aws lambda get-function-configuration --function-name "$FN" --region "$REGION" \
      --query MemorySize --output text)
echo "function=$FN region=$REGION memory=${MEM}MB"
echo

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

invoke() {   # -> writes body to $TMP/out, prints "init billed duration"
	local logs
	logs=$(aws lambda invoke --function-name "$FN" --region "$REGION" \
	        --log-type Tail --cli-binary-format raw-in-base64-out \
	        --payload '{}' "$TMP/out" --query LogResult --output text \
	        | base64 --decode)
	local init billed dur
	init=$(printf '%s' "$logs"   | sed -n 's/.*Init Duration: \([0-9.]*\) ms.*/\1/p'   | tail -1)
	billed=$(printf '%s' "$logs" | sed -n 's/.*Billed Duration: \([0-9]*\) ms.*/\1/p' | tail -1)
	dur=$(printf '%s' "$logs"    | sed -n 's/.*\bDuration: \([0-9.]*\) ms.*/\1/p'     | head -1)
	echo "${init:-0} ${billed:-0} ${dur:-0}"
}

force_cold() {
	aws lambda update-function-configuration --function-name "$FN" --region "$REGION" \
	  --environment "Variables={PRP_REPS=${PRP_REPS:-10},PRP_PRIMORIAL=${PRP_PRIMORIAL:-2357},COLD=$RANDOM$RANDOM}" \
	  >/dev/null
	aws lambda wait function-updated --function-name "$FN" --region "$REGION"
}

echo "--- cold starts (n=$COLD_N) ---"
COLD_SUM=0
for i in $(seq 1 "$COLD_N"); do
	force_cold
	read -r init billed dur <<<"$(invoke)"
	printf "  cold %d: init=%8s ms  duration=%9s ms  billed=%6s ms\n" "$i" "$init" "$dur" "$billed"
	COLD_SUM=$(awk -v a="$COLD_SUM" -v b="$init" 'BEGIN{print a+b}')
done
COLD_AVG=$(awk -v s="$COLD_SUM" -v n="$COLD_N" 'BEGIN{printf "%.1f", s/n}')

echo
echo "--- warm invocations (n=$WARM_N) ---"
BILLED_SUM=0; CYCLES=""
for i in $(seq 1 "$WARM_N"); do
	read -r init billed dur <<<"$(invoke)"
	c=$(sed -n 's/.*ticks_per_test=\([0-9]*\).*/\1/p' "$TMP/out" | head -1)
	printf "  warm %d: duration=%9s ms  billed=%6s ms  cycles/PRP=%s\n" "$i" "$dur" "$billed" "${c:-?}"
	BILLED_SUM=$(awk -v a="$BILLED_SUM" -v b="$billed" 'BEGIN{print a+b}')
	[ -n "${c:-}" ] && CYCLES="$CYCLES $c"
done

echo
echo "--- payload from the last invocation ---"
sed 's/^/  /' "$TMP/out"

CYC_MED=$(printf '%s\n' $CYCLES | sort -n | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}')
REPS=$(aws lambda get-function-configuration --function-name "$FN" --region "$REGION" \
       --query 'Environment.Variables.PRP_REPS' --output text 2>/dev/null || echo 10)

echo
echo "=== summary ==="
printf "  cold start (Init Duration), mean of %d : %s ms\n" "$COLD_N" "$COLD_AVG"
printf "  median cycles per PRP test            : %s\n" "${CYC_MED:-unknown}"

# Cost per 7.3e9 PRP tests -- one expected Cunningham k=6 chain, so the number
# lands in the same units the rest of this repo reasons in.
awk -v billed="$BILLED_SUM" -v n="$WARM_N" -v reps="$REPS" -v mem="$MEM" \
    -v pg="$PRICE_GB_SEC" -v pr="$PRICE_REQ" '
BEGIN {
  ms_per_inv = billed / n;
  ms_per_test = ms_per_inv / reps;
  gb = mem / 1024.0;
  cost_per_inv = (ms_per_inv/1000.0) * gb * pg + pr;
  cost_per_test = cost_per_inv / reps;
  printf "  billed ms per PRP test                : %.2f\n", ms_per_test;
  printf "  cost per PRP test                     : $%.3e\n", cost_per_test;
  printf "  cost per 7.3e9 tests (one k=6 chain)  : $%.0f\n", cost_per_test*7.3e9;
  printf "  effective $ per core-hour             : $%.4f\n", gb*pg*3600;
}'
echo
echo "  Compare: BareMetal Cloud \$0.00501056/core-hour."
echo "  The request charge is negligible at this duration and dominant at high churn;"
echo "  it is included above so short-invocation regimes are not flattered."
