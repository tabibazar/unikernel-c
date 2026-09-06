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
# One worker, not three. Ian Seyler confirmed on 2026-09-05 that in the alpha
# all BareMetal Cloud instances share a single vCPU, so additional instances add
# no compute for a CPU-bound job -- three workers divide one core three ways and
# cost three times as much for the same throughput. Raise this only when the
# platform scales out.
WORKERS="${WORKERS:-w0}"

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

if ! docker info >/dev/null 2>&1; then
	echo "docker is not running -- the worker build needs it. Start Docker Desktop." >&2
	exit 1
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
		id=$(echo "$line" | awk '{print $1}')
		status=$(echo "$line" | awk '{print $3}')

		# Three cases, and the missing one matters. An earlier version skipped
		# a worker that did not exist at all, so once a failed build left no
		# instance behind the tender idled forever waiting for something to
		# stop. Missing is now a reason to deploy, not a reason to do nothing.
		if [ -z "$line" ]; then
			say "cc-$w is missing entirely -- deploying it"
			id=""
		elif [ "$status" = "STOPPED" ]; then
			say "cc-$w ($id) has stopped -- harvesting"
			stamp=$(date -u +%Y%m%dT%H%M%SZ)
			out="$LOGS/${w}-${stamp}.log"
			"$API" instances logs "$id" > "$out" 2>&1
			hits=$(grep -c WORKER_HIT "$out" 2>/dev/null || echo 0)
			done_line=$(grep WORKER_DONE "$out" 2>/dev/null | tail -1)
			say "  harvested $(wc -l < "$out" | tr -d ' ') lines, $hits hits; $done_line"
		else
			continue          # RUNNING -- leave it alone
		fi

		# Build the replacement FIRST, before destroying anything. The
		# previous order was harvest, delete, build -- so a failed build left
		# no worker running at all, which is exactly what a reboot that took
		# Docker with it produced. The old instance is already STOPPED and
		# costs nothing while the build runs, and the instance cap has room
		# for it, so there is no reason to destroy it early.
		lo=$(cat "$NEXT_RANGE")
		hi=$((lo + SLICE_COUNT))
		say "  rebuilding cc-$w on m=[$lo,$hi) before retiring the old one"
		if ! "$REPO/scripts/build-worker-docker.sh" "$w" "$lo" "$SLICE_COUNT" >>"$TENDER_LOG" 2>&1; then
			say "  build FAILED for cc-$w -- old instance left in place, range unclaimed"
			continue
		fi
		echo "$hi" > "$NEXT_RANGE"

		# Now the replacement exists, so destroying the old one is safe.
		# The API refuses to delete a RUNNING instance ("must be in one of
		# [PENDING, STOPPED, SUSPENDED, SNAPSHOTTED, ERROR]"), so stop first.
		if [ -n "$id" ]; then
			"$API" instances stop "$id" >/dev/null 2>&1
			"$API" instances rm "$id" >/dev/null 2>&1
		fi
		# Image id tracked here rather than read back from `instances list`,
		# which prints only id, name and status -- an earlier version parsed a
		# fourth column that does not exist and never deleted anything.
		old_img=""
		[ -f "$STATE/img-$w" ] && old_img=$(cat "$STATE/img-$w")

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
		# Re-read rather than using the value from the top of the pass: with
		# several workers redeployed in one pass they all wrote deploys+1, so
		# the budget counted one instead of N and the cap would not have bound.
		echo $(( $(cat "$DEPLOY_COUNT") + 1 )) > "$DEPLOY_COUNT"
		say "  cc-$w redeployed as $instid on m=[$lo,$hi)  [$((deploys + 1))/$MAX_DEPLOYS]"
	done

	sleep "$POLL_SECONDS"
done
