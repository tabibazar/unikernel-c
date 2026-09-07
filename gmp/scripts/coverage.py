#!/usr/bin/env python3
"""coverage.py -- what has actually been searched, derived from finished work.

The cloud side kept a forward counter (`next_range`) as its record of progress,
and advanced it when a slice was *deployed* rather than when it *finished*. Two
things then put holes in the search without anything reporting an error:

  * a manual redeploy read a counter that had already been advanced, skipping
    m=[100.315e9, 100.630e9) entirely -- 315 million multipliers;
  * two slices were killed mid-flight during the scale-down to one worker, at
    18% and 16% complete, while their ranges were already marked consumed.

Together that is ~489 million multipliers, 14% of the claimed span, never
examined. A negative result over a range with a hole in it is not a negative
result, and nothing in the system would have said so.

The local hunt has no such holes -- 2,578 chunks, zero gaps, zero overlap --
precisely because it advances its counter only after a chunk completes. This
script brings the cloud side to the same standard by making the *logs* the
record rather than the counter: an interval counts as searched only when a
WORKER_DONE line says the whole slice was examined.

A partially-finished slice is treated as entirely unsearched. Its survivors are
tested in increasing m order, so the boundary could in principle be recovered by
re-sieving and finding the Nth survivor -- but re-running 105 million
multipliers costs about two hours of a shared vCPU, and getting that arithmetic
subtly wrong would reintroduce exactly the silent hole this script exists to
remove. Redundant work is the cheaper mistake.

    ./coverage.py            # report
    ./coverage.py --queue    # also write holes to pending_ranges for the tender
"""
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGS = os.path.join(REPO, "results/hunt-local/cloud/logs")
PENDING = os.path.join(REPO, "results/hunt-local/cloud/pending_ranges")
SLICE = 105_000_000


def slices():
    """Every slice we ever deployed, with whether it actually finished."""
    out = []
    for f in sorted(glob.glob(os.path.join(LOGS, "*.log"))):
        t = open(f, errors="ignore").read()
        m = re.search(r"WORKER_START.*?m=\[(\d+),(\d+)\)", t)
        if not m:
            continue
        lo, hi = int(m.group(1)), int(m.group(2))
        done = re.search(r"WORKER_DONE.*?examined=(\d+)", t)
        prog = re.findall(r"WORKER_PROGRESS.*?done=(\d+)", t)
        tested = int(done.group(1)) if done else (int(prog[-1]) if prog else 0)
        out.append(dict(lo=lo, hi=hi, done=bool(done), tested=tested,
                        log=os.path.basename(f)))
    return sorted(out, key=lambda s: (s["lo"], s["hi"]))


def merge(intervals):
    """Union of half-open intervals."""
    out = []
    for lo, hi in sorted(intervals):
        if out and lo <= out[-1][1]:
            out[-1][1] = max(out[-1][1], hi)
        else:
            out.append([lo, hi])
    return [tuple(x) for x in out]


def main():
    s = slices()
    if not s:
        print("no cloud logs found")
        return 1

    finished = merge([(x["lo"], x["hi"]) for x in s if x["done"]])
    claimed_lo = min(x["lo"] for x in s)
    claimed_hi = max(x["hi"] for x in s)

    # holes = claimed span minus what actually finished
    holes = []
    cur = claimed_lo
    for lo, hi in finished:
        if lo > cur:
            holes.append((cur, lo))
        cur = max(cur, hi)
    if cur < claimed_hi:
        holes.append((cur, claimed_hi))

    swept = sum(hi - lo for lo, hi in finished)
    hole_n = sum(hi - lo for lo, hi in holes)

    print(f"slices deployed   : {len(s)}")
    print(f"slices finished   : {sum(1 for x in s if x['done'])}")
    print(f"claimed span      : {claimed_lo:,} .. {claimed_hi:,}"
          f"  ({claimed_hi - claimed_lo:,})")
    print(f"actually searched : {swept:,}")
    print(f"holes             : {hole_n:,}"
          f"  ({100 * hole_n / (claimed_hi - claimed_lo):.1f}% of the span)")

    if not s or not any(not x["done"] for x in s):
        pass
    else:
        print("\nunfinished slices (whole range requeued):")
        for x in s:
            if not x["done"]:
                print(f"  [{x['lo']:,} , {x['hi']:,})  only {x['tested']:,} tested"
                      f"  ({x['log']})")

    print("\nholes to re-search:")
    for lo, hi in holes:
        print(f"  [{lo:,} , {hi:,})  = {hi - lo:,}")
    if not holes:
        print("  none -- coverage is contiguous")

    if "--queue" in sys.argv:
        # split holes into slice-sized work units the tender can consume
        units = []
        for lo, hi in holes:
            x = lo
            while x < hi:
                units.append((x, min(x + SLICE, hi)))
                x += SLICE
        with open(PENDING, "w") as fh:
            for lo, hi in units:
                fh.write(f"{lo} {hi}\n")
        print(f"\nwrote {len(units)} work units to {PENDING}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
