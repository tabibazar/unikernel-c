#!/bin/bash
# cloud-tender.sh -- keep the BareMetal Cloud workers fed.
#
# A worker's slice is baked into its image, so a worker finishes -- roughly two
# hours for ~296,000 survivors -- and then stops. Left alone, three instances
# would work for two hours and idle for the remaining seventy of a three-day
# run. This polls for finished workers, harvests their console output, and
# redeploys them onto the next unclaimed range.
#
# WHY THIS AND NOT A CRON JOB
#
# The obvious alternative fires once a day, which would waste most of the
# available time for the same reason. This is a loop on a machine that is
# already running the local hunt, so it costs nothing.
#
# SAFETY, BECAUSE THIS SPENDS MONEY UNATTENDED
#
# Instances cost $0.00501056/hr each. Three of them for three days is about
# $1.08 against $23 of credit, so the intended spend is small -- but a loop
# that redeploys in a tight cycle because of a bug it cannot see would not be.
# Hence:
#
#   * only instances whose name starts with cc-w are ever touched. bmagent and
#     anything else a human created are invisible to this script.
#   * MAX_DEPLOYS caps the total number of redeploys for the whole run. When it
#     is reached the script stops rather than continuing quietly.
#   * a file called STOP in the state directory halts it at the next poll.
#   * every harvested log is written to disk before the instance that produced
#     it is deleted, because the console is the only copy.
#
#   ./cloud-tender.sh          # run until stopped
#   ./cloud-tender.sh --status
#
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATE="${STATE:-$REPO/results/hunt-local/cloud}"
LOGS="$STATE/logs"
NEXT_RANGE="$STATE/next_range"
DEPLOY_COUNT="$STATE/deploy_count"
TENDER_LOG="$STATE/tender.log"

API="$REPO/scripts/vendor/bm-api.sh"
SLICE_COUNT="${SLICE_COUNT:-105000000}"
POLL_SECONDS="${POLL_SECONDS:-300}"
MAX_DEPLOYS="${MAX_DEPLOYS:-60}"
WORKERS="${WORKERS:-w0 w1 w2}"

mkdir -p "$LOGS"
[ -f "$NEXT_RANGE" ]   || echo 100315000000 > "$NEXT_RANGE"   # see RANGES.md
[ -f "$DEPLOY_COUNT" ] || echo 0 > "$DEPLOY_COUNT"

export BM_API_KEY="${BM_API_KEY:-$(cat "$HOME/.bm_api_key" 2>/dev/null | tr -d '\n\r ')}"
[ -n "$BM_API_KEY" ] || { echo "no BM_API_KEY and no ~/.bm_api_key" >&2; exit 1; }

say() { printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" | tee -a "$TENDER_LOG"; }

if [ "${1:-}" = "--status" ]; then
	echo "next free range : $(cat "$NEXT_RANGE")"
	echo "redeploys used  : $(cat "$DEPLOY_COUNT") of $MAX_DEPLOYS"
	echo "harvested logs  : $(ls -1 "$LOGS" 2>/dev/null | wc -l | tr -d ' ')"
	echo "hits so far     : $(grep -h WORKER_HIT "$LOGS"/*.log 2>/dev/null | wc -l | tr -d ' ')"
	"$API" instances list 2>/dev/null
	exit 0
fi

say "tender starting; next range $(cat "$NEXT_RANGE"), budget $(cat "$DEPLOY_COUNT")/$MAX_DEPLOYS"

while :; do
	[ -f "$STATE/STOP" ] && { say "STOP file present -- exiting"; exit 0; }

	deploys=$(cat "$DEPLOY_COUNT")
	if [ "$deploys" -ge "$MAX_DEPLOYS" ]; then
		say "redeploy budget exhausted ($deploys) -- exiting rather than spending more"
		exit 0
	fi

	listing=$("$API" instances list 2>/dev/null)
	if [ -z "$listing" ]; then
		say "instance list empty or API unreachable -- will retry"
		sleep "$POLL_SECONDS"; continue
	fi

	for w in $WORKERS; do
		# Only ever act on our own workers.
		line=$(echo "$listing" | grep -E "[[:space:]]cc-$w[[:space:]]" | head -1)
		[ -n "$line" ] || continue
		id=$(echo "$line" | awk '{print $1}')
		status=$(echo "$line" | awk '{print $3}')
		[ "$status" = "STOPPED" ] || continue

		say "cc-$w ($id) has stopped -- harvesting"

		stamp=$(date -u +%Y%m%dT%H%M%SZ)
		out="$LOGS/${w}-${stamp}.log"
		"$API" instances logs "$id" > "$out" 2>&1
		hits=$(grep -c WORKER_HIT "$out" 2>/dev/null || echo 0)
		done_line=$(grep WORKER_DONE "$out" 2>/dev/null | tail -1)
		say "  harvested $(wc -l < "$out" | tr -d ' ') lines, $hits hits; $done_line"

		# Only now is it safe to destroy the instance: the console was the
		# only copy of that work.
		"$API" instances rm "$id" >/dev/null 2>&1
		# The image this instance was created from is tracked here rather than
		# read back from `instances list`, which prints only id, name and
		# status -- an earlier version parsed a fourth column that does not
		# exist, silently never deleted anything, and would have accumulated
		# an image per redeploy.
		old_img=""
		[ -f "$STATE/img-$w" ] && old_img=$(cat "$STATE/img-$w")

		lo=$(cat "$NEXT_RANGE")
		hi=$((lo + SLICE_COUNT))
		say "  rebuilding cc-$w on m=[$lo,$hi)"
		if ! "$REPO/scripts/build-worker-docker.sh" "$w" "$lo" "$SLICE_COUNT" >>"$TENDER_LOG" 2>&1; then
			say "  build FAILED for cc-$w -- leaving the range unclaimed and moving on"
			continue
		fi
		echo "$hi" > "$NEXT_RANGE"

		# Retire the previous image BEFORE uploading the replacement. There is
		# an undocumented cap of 10 images per account -- /api/limits reports
		# vcpu, ram and instance caps but says nothing about images -- and the
		# safer-looking order (upload, then delete the old one) deadlocks
		# against it: the upload fails because the quota is full, so the old
		# image is never retired, so the next upload fails too. That is exactly
		# how all three workers stayed down overnight.
		#
		# Deleting first is safe here because the instance using this image was
		# already destroyed above, and the image is reproducible from the slice
		# in any case.
		if [ -n "$old_img" ]; then
			"$API" images rm "$old_img" >/dev/null 2>&1 && say "  retired old image $old_img"
		fi

		imgid=$("$API" images upload "cc-$w" "$REPO/results/workers/$w/baremetal.elf" 2>/dev/null | awk -F': ' '/^id:/{print $2}')
		if [ -z "$imgid" ]; then
			say "  upload failed for cc-$w (image quota? run: $API images list)"
			continue
		fi
		instid=$("$API" instances create "cc-$w" 1 16 "$imgid" 2>/dev/null | awk -F': ' '/^id:/{print $2}')
		if [ -z "$instid" ]; then say "  create failed for cc-$w"; continue; fi

		echo "$imgid" > "$STATE/img-$w"
		echo $((deploys + 1)) > "$DEPLOY_COUNT"
		say "  cc-$w redeployed as $instid on m=[$lo,$hi)  [$((deploys + 1))/$MAX_DEPLOYS]"
	done

	sleep "$POLL_SECONDS"
done
