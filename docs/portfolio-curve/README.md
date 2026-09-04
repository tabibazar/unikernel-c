# How far can you split a search before it stops working?

The proposal in [`../portfolio/PROPOSAL.md`](../portfolio/PROPOSAL.md) named one
question and left it unanswered:

> At equal total compute, does a portfolio of N short independent runs beat one
> long run — and if so, at what length?

It matters because it decides whether this repo's substrate argument stands up.
A BareMetal unikernel's only real edge is cheap ephemeral fan-out, and three
research passes kept finding that the record-setting engines in this space are
single long serial trajectories. If short restarts lose badly, the fan-out pitch
is wrong.

## The design

`pcb442`, published optimum 50,778. Every arm spends **exactly 200,000 ant
constructions**, split different ways. The combination operator is `min` over
the portfolio — the one operator that needs no communication between workers,
which is the whole point. Five replicates per arm, independent seeds throughout.

Roughly 4 core-hours, about 25 minutes on a 10-core laptop. No cloud spend.

## The result

| workers | iterations each | seconds each | mean tour | sd | vs one long run | σ apart |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 200,000 | 359.7 | 51,478.0 | 83.4 | — | — |
| 10 | 20,000 | 36.0 | 51,490.2 | 79.6 | +0.02% | 0.15 |
| 40 | 5,000 | 9.0 | 51,473.4 | 49.7 | −0.01% | −0.07 |
| 100 | 2,000 | 3.6 | 51,536.0 | 65.1 | +0.11% | 0.78 |
| **200** | **1,000** | **1.8** | **51,504.2** | 55.0 | **+0.05%** | **0.37** |
| 400 | 500 | 0.9 | 51,600.2 | 21.5 | +0.24% | 2.01 |
| 1,000 | 200 | 0.36 | 51,921.2 | 66.0 | +0.86% | 5.89 |
| 10,000 | 20 | 0.04 | 52,329.8 | 79.0 | +1.65% | 10.49 |

**Splitting the work 200 ways is free.** One long run and two hundred short
independent ones are statistically indistinguishable — every arm from 1 to 200
sits under one standard deviation of the single-run baseline, and the 40-worker
arm is nominally *ahead* of it.

Then it falls off a cliff. At 400 workers the penalty is marginal (2.0σ), at
1,000 it is unambiguous (5.9σ), and at 10,000 the portfolio is 1.65% worse than
simply running once.

## What the cliff actually is

Not a limit on worker count — a **floor on task granularity**. The knee sits
between 500 and 1,000 iterations per worker, which on this instance is between
0.9 and 1.8 seconds of work.

Below that a worker cannot finish what it starts. MMAS spends its first
iterations building pheromone structure from a nearest-neighbour tour, and a
run that dies before that structure forms contributes a barely-improved starting
tour to the `min`. Ten thousand of those are worth less than one run that got
somewhere.

## Why this is a positive result for the substrate

The design rule falls straight out: **give each worker at least a couple of
seconds of real work, and you can then have as many workers as you like.**

Against that floor, a 31 ms boot is under 2% overhead — which is precisely the
regime where an ephemeral unikernel is the right shape. A 950 ms Linux guest
boot would be 50% overhead at the same granularity, and AWS Lambda's measured
45 ms cold start would be about 2.5%.

So the fan-out story survives, with a number attached to it rather than a hope.


## All three instances, 2026-09-04

`pcb442` above; `kroA100` and `rat783` added to complete the proposal's
three-instance requirement. Same protocol throughout. Data in
[`results-kroA100-rat783.tsv`](results-kroA100-rat783.tsv).

### rat783 (optimum 8,806) -- the same shape, sharper

| workers | iters each | mean | vs one long run | σ apart |
|---:|---:|---:|---:|---:|
| 1 | 200,000 | 9,103.4 | — | — |
| 10 | 20,000 | 9,095.4 | −0.09% | −1.13 |
| **40** | **5,000** | **9,093.8** | **−0.11%** | −0.80 |
| 100 | 2,000 | 9,096.2 | −0.08% | −1.39 |
| 200 | 1,000 | 9,109.2 | +0.06% | 1.22 |
| 400 | 500 | 9,119.6 | +0.18% | 1.18 |
| 1,000 | 200 | 9,205.0 | +1.12% | **14.72** |
| 10,000 | 20 | 9,294.8 | +2.10% | **15.51** |

The plateau and the cliff reproduce, and the cliff is in the same place: it
arrives between 400 and 1,000 workers on both instances. On the larger problem
the fall is steeper — 14.7σ rather than 5.9σ — which is what one would expect
if the floor is about a worker finishing what it starts, since a bigger
instance needs more iterations to get anywhere.

Four arms are nominally *ahead* of the single long run here. None of them
significantly so.

### kroA100 (optimum 21,282) -- saturated, and therefore uninformative

Every arm, at every granularity, found the published optimum exactly. Standard
deviation zero, all the way down to ten thousand workers doing twenty
iterations each.

That is not a result about fan-out; it is a statement that this instance is too
easy for the budget. It is reported rather than dropped because it counts
toward the proposal's three-instance gate, and because a row of identical
numbers is exactly the shape a broken harness also produces — the check is that
the same binaries give a spread on the other two instances.

## Reading the gate

The proposal's pre-registered criterion refuses the fan-out thesis if a single
long trajectory **matches or beats the best portfolio granularity on at least
two of three instances**.

| instance | single long run | best portfolio | single run matches or beats? |
|---|---:|---:|---|
| pcb442 | 51,478.0 | 51,473.4 (40 workers) | no |
| rat783 | 9,103.4 | 9,093.8 (40 workers) | no |
| kroA100 | 21,282.0 | 21,282.0 (tie at optimum) | yes, vacuously |

One of three, and that one carries no information. **The pre-registered
falsification is not triggered.**

Two cautions on how much that is worth. The portfolio's nominal wins on pcb442
and rat783 are inside the noise — 0.37σ and 0.80σ — so the honest claim is
*"splitting the work does not cost anything"*, not *"splitting the work helps"*.
And the criterion turned on a comparison of two means that differ by less than
their own scatter, which is a thin edge on which to hang a thesis either way.

What the three instances do support jointly, and much more strongly than the
gate does, is the granularity floor: **fan-out is free up to a few hundred
workers and collapses below roughly 500 iterations of work each**, on both
instances where the problem was hard enough to measure it.

## What this does not show

**It is one workload.** MMAS has an unusually long memory — pheromone
accumulates across iterations — which arguably makes it the *least*
fan-out-friendly search one could pick. That the plateau extends to 200 workers
anyway is encouraging, but it is evidence about ant colony optimization on TSP,
not about search in general.

**The prediction going in was wrong**, and it is recorded here because it was
written down first: the expectation was monotonic degradation with fan-out,
with one long run winning outright. The truth is a plateau and then a cliff, and
the plateau is much wider than expected.

**Seconds are instance-specific.** The 1.8-second floor is 1,000 iterations on
pcb442 at ~556 iterations/second. The transferable quantity is the iteration
count, not the wall-clock.

## Reproducing

Each arm is a separate build, because iteration count is compile-time in
`aco.c` (BareMetal passes no `argv`):

```sh
cc -O2 -std=c99 -DACO_INSTANCE_HEADER='"instances/pcb442.h"' \
   -DACO_ITERS=1000 -DACO_LOCAL_SEARCH=1 -DACO_DIST_CACHE=1 -o aco_1000 aco.c
```

Raw data in [`results.tsv`](results.tsv): arm, runs, iterations, replicate,
best tour found by that portfolio.
