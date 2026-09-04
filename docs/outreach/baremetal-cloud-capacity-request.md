# Draft: capacity request to Return Infinity

**Status:** drafted in Gmail, NOT sent. Awaiting Reza's read.
**To:** varun.madhok@returninfinity.com (CEO -- the cap is his call)
**Cc:** ian.seyler@returninfinity.com (CTO)
**Subject:** Asking for more instances -- a record attempt on BareMetal Cloud
**Thread:** new, deliberately not buried in the 2026-09-04 bug-report thread.

**Ask in one line:** raise `maxInstancesPerUser` from 4 to ~100 for a bounded
window, to attempt a public record on BareMetal Cloud.

The live text is the Gmail draft; it was cut to roughly half the first version
on request. Structure: ask -> what blocks it -> what we would accept instead ->
three measured results from the ring3 build -> the tap checksum gotcha -> replies
to Ian's three points.

The load-bearing number is **222 days at four cores against nine days at a
hundred, for about $107 either way** -- the cap changes the calendar, not the
cost. Everything else is supporting material.

Two things kept despite the cut: the retraction of the 3.7x claim (an admission,
but parity is good news for them and it buys credibility for the other figures),
and the tap checksum finding (the most directly useful thing in it for Ian).

## Notes for us, not for them

- Sources: `gmp/results/prp-ring3-2026-09-04.txt` (10.517 ms, parity with
  Linux), `netbench/results/inbound-ring3-2026-09-04.md` (1200 requests),
  `pyprobe/results/RESULTS.md` (40 probes), `docs/upstream-reports/`.
- The fallback if they say no is AWS **spot**, not on-demand: c5.metal spot is
  $0.47-0.60/hr against $4.08 on-demand, and the workload is interruptible.
  Never on-demand.
- Do not oversell the record. A k=6 find is probabilistic; the honest framing
  is a ~95% chance at 3x expectation, not a certainty.
