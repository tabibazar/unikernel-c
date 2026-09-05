# BareMetal Cloud instances are ~7x slower than assumed, and that changes the price

The first three cloud slices completed overnight. They took far longer than
planned, and the reason matters more than the delay.

## The measurement

Identical code -- `cc_worker` built from the same source, same `WORKER_REPS=8`,
same 1011-digit operands -- run in two places:

| | ms per survivor |
|---|---:|
| this Mac (M-series core) | 8.1 |
| BareMetal Cloud instance | **58.2** |

**7.2x slower.** The cloud figure is a clean measurement, not an estimate: the
instance booted at 00:52:58 and printed `Finished Payload` at 05:39:50, having
examined 295,887 survivors. That is 17,212 s, or 58.2 ms each.

## Why it matters: the price comparison was right per core-hour and wrong per unit of work

The capacity request sent to Return Infinity on 2026-09-04 says their rate and
c5.metal spot "are the same price to within 2%". Per *instance-hour* against
*core-hour*, that is true:

| | $/hour |
|---|---:|
| BareMetal Cloud (1 vCPU) | 0.00501056 |
| c5.metal spot, per core | 0.004916 |

But an hour buys 7.2x less work on one than the other, and it is work that is
being bought. Per PRP test:

| | $ per test |
|---|---:|
| BareMetal Cloud | 8.10e-08 |
| c5.metal spot | 1.11e-08 |

**BareMetal Cloud is about 7x more expensive per unit of work**, not equal.
Restated as the chain hunt:

| | |
|---|---:|
| on BareMetal Cloud | ~153,000 instance-hours, ~$767 |
| on c5.metal spot | 21,326 core-hours, ~$105 |

That statement in the email needs correcting, and it should be corrected
proactively rather than left standing -- it is the kind of error that looks like
spin if they find it themselves.

## What this does not establish

The 8.1 ms baseline is an Apple M-series core, not a Xeon, so this is not a
clean measurement of their hardware against AWS hardware. The honest claim is
narrower: *the same workload costs 7.2x more wall-clock on a BareMetal Cloud
instance than on this development machine*, and the cost consequence follows
from that regardless of what the reference core happens to be.

Nor does it identify the cause. Plausible candidates, none tested:

- an older or lower-clocked host CPU
- oversubscription -- one vCPU sharing a physical core with other tenants
- something in the 16 MiB envelope hurting GMP's working set

The third is the one worth ruling out first, because it would be ours to fix
rather than theirs. It is also the least likely: at 1011 digits the operands are
about 420 bytes and GMP's scratch space is small.

## Consequence for the ask

More instances still help -- the work is embarrassingly parallel and the cap is
still the thing that decides the calendar. But the honest framing of the
platform changes: it is not "the same price, and we prefer your stack". It is
"your stack is the better fit, and we would like to understand the throughput
gap before committing a budget to it". That is a better conversation to have
with them anyway, and it hands them something worth knowing about their own
product.
