# A restart portfolio on ephemeral microVMs

**A proposal for Return Infinity.** Draft, 2026-09-02. Repo `unikernel-c`.

## The proposal in one paragraph

There is a class of computation that no general-purpose OS is shaped for:
hundreds of thousands of short, independent, randomly-seeded searches, each
holding no state, each discarded when it finishes. Every published record in
combinatorial optimisation is currently held by the opposite thing -- one long
serial trajectory on one thread. **Nobody has measured which wins at equal
compute.** This proposal is that measurement, on an open benchmark with a
public scoreboard, for about $250 of compute. It is designed so that a
negative result is as publishable as a positive one, and so that it produces,
either way, a defensible answer to the question Return Infinity needs answered
before BareMetal can be sold for anything but serving: *which workloads is this
substrate actually the right host for?*

## Why this question, and not a more exciting one

Six candidate targets were researched against a fixed budget. Five are out, and
the reasons are worth stating because they are the reasons a reader will
propose them.

| target | standing frontier | why it is out |
|---|---|---|
| Collatz verification | 2075x2^60 (~2^71.02), Jan 2025 | Cost 12,395 CPU-years + 159 GPU-years on national supercomputers. GPUs clear a work unit ~52x faster than a Xeon thread. Four to five orders of magnitude out, on the wrong hardware. |
| Busy Beaver BB(6) | ~1,003 holdouts, all simulated to 1e13 steps | **Idea-limited, not compute-limited.** Antihydra reduces BB(6) to an open Collatz-like question; it is already simulated to a > 2^37 with residual halting probability ~1e-28723042565. More cycles buy no probability mass. |
| Schur number S(5) | Settled: S(5)=160, Heule AAAI-18 | The one target whose cube-and-conquer decomposition genuinely matched ephemeral fan-out is a **closed record**. Its successor S(6) is unpriced. |
| QAP tai256c optimality | 1.25% UB/LB gap, Optim. Lett. 2024 | Authors estimate 6.7e16 relaxation subproblems, ~2.6e12 days. Eight-plus orders out. |
| GIMPS Mersenne | All exponents < 141,340,919 tested once | A genuine lottery with cheap certificates, and a marginal contributor's odds do not dilute with network size -- but it is a ticket purchase, not an experiment. It answers no question. |
| **MIPLIB 2017 open instances** | ~240 open; incumbents turn over in months | **In.** See below. |

MIPLIB is in for three reasons, and only the first is about ambition.

**The winning runs are short enough to portfolio.** Local-ILP (arXiv:2305.00188)
set new best-known objective values on six open instances using **one thread**
and per-run time limits of **10, 60 and 300 seconds**. That is the only target
found where a single successful run is short enough that a budget buys a
statistically meaningful number of them. Everything else needs core-centuries.

**A result is verified, not claimed.** MIPLIB validates a submission as
feasibility plus objective value -- seconds of arithmetic, with per-submission
attribution on the public instance page. Compare a claimed exhaustive bound,
which nobody can check and nobody should accept from an unknown group.

**The restart structure is native.** A MIP local search is already randomised
and restart-driven. We are not inventing a parallel algorithm; we are running
the published serial one many times with different seeds. That is exactly the
deployment pattern this repo already runs for prime search.

## The question, stated so it can fail

> At equal total core-hours, does a portfolio of N short independent restarts
> beat a single long trajectory -- and if so, at what restart length?

The deliverable is not a yes or no. It is a **curve**: best objective found as a
function of restart length, at fixed total compute, from one long run down to
hundreds of thousands of short ones. The curve has a minimum somewhere, and
where that minimum sits is the whole finding:

- **Minimum at hours.** Long trajectories win. Ephemeral fan-out is the wrong
  shape for this problem class, and by extension for combinatorial search
  generally. Buy large machines and run them for a long time. *This kills the
  substrate thesis, cheaply, and that is a good outcome.*
- **Minimum at seconds to low minutes.** Fan-out wins, and the per-worker
  overhead of the host becomes the binding cost -- which is the regime where a
  31 ms boot is worth something and a 950 ms one is not.

**Pre-registered falsification:** if a single long trajectory matches or beats
the best portfolio granularity at equal core-hours on **at least two of three
instances**, the ephemeral-fan-out thesis is refused and the study reports that
instead. This is written before any number is seen, for the same reason the
prior study in this repo pre-registered its gate: a criterion adopted after
seeing results is not a criterion.

## What the substrate actually contributes -- measured, not claimed

This repo has already measured BareMetal head to head against Nanos and Linux
on one machine (`../nanos-vs-baremetal/`). The numbers do not all point the
same way, and the proposal is built on the honest version.

| | BareMetal | Nanos | Linux guest (Firecracker) | Linux process |
|---|---:|---:|---:|---:|
| boot to first app instruction | **0.031 s** | 0.240 s | 0.95 s | 0.027 s |
| smallest working memory | **4 MiB** | 16 MiB | -- | -- |
| complete artifact | **1.32 MB** | 14.2 MB image | 20.2 MB vmlinux | -- |
| fixed-work compute | **51.7 s** | 12.9 s | 14.1 s | 12.9 s |

Read the last row first. **BareMetal is 3.7x slower on compute**, and this repo
has eliminated three plausible causes -- it is not the missing `-O2` (8% on this
workload), not the hypervisor (Linux under the same Firecracker lands at
14.1 s), not Nanos-style unikernel overhead in general (Nanos costs under 1%).
The cause is unisolated.

Now the honest consequence, which the proposal does not hide:

**Boot advantage does not repay a 3.7x compute penalty.** At a 10-second
restart, a 31 ms boot is 0.31% overhead against a Linux *guest's* 8.7% -- an
8-point saving against a 270-point deficit. And against a native Linux
**process**, which boots in 0.027 s, BareMetal's boot advantage is zero. If the
comparison is "Linux processes on a big box", BareMetal loses this experiment on
throughput and nothing else matters.

So the substrate case rests on exactly two things, and both should be stated as
open rather than assumed:

1. **Isolation per task, at 4 MiB.** BareMetal runs where Nanos will not boot,
   in an artifact smaller than the Nanos kernel alone. If workers are ever
   untrusted, or tenanted, or need to be reclaimed hard, a 1.32 MB image with a
   31 ms boot is a different economic object from a Linux VM. Our own workers
   are trusted, so **this experiment does not test that** -- it only establishes
   whether the workload shape exists at all.
2. **The 3.7x is a defect, not a law.** Nanos demonstrates that a unikernel need
   not pay it. If Return Infinity isolates and fixes it, every column above
   points the same way and the substrate case is made. If it stands, this
   workload runs on Linux and BareMetal's answer to "what is this for" stays
   unanswered.

**This is the concrete ask.** The most valuable thing in this proposal for
Return Infinity is not the benchmark record. It is that the 3.7x is the single
measurement standing between BareMetal and a named workload class, and this
experiment is designed so that fixing it flips the verdict.

## The fault-tolerance argument, delivered properly

This repo established by controlled experiment that the platform computes
incorrectly about once in 1.6 million operations -- `mod_bad=127` in 200,000,000,
zero with `cli`, root-caused to register preservation in the interrupt path
(`../arithmetic-fault/`). A prior study tried to argue that Ant Colony
Optimization structurally absorbs that fault. The argument was sound but soft:
it rested on evaporation erasing perturbations, which is a claim about
dynamics.

A restart portfolio makes the same claim **by construction, not by dynamics**:

- Every worker returns a candidate solution, not a bound and not a summary.
- The collector **recomputes the objective on trusted hardware** and checks
  feasibility. A solution is accepted on arithmetic the guest did not perform.
- A corrupted worker therefore cannot inject a wrong answer. It can only
  produce a worse one, or an invalid one that is discarded. The cost of a fault
  is one worker's runtime -- bounded, known, and at 6.35e-7 per operation,
  negligible against the loss from an unlucky seed.

This is the same discipline the prior study's migration register already used:
adopted tours were revalidated on receipt -- permutation check plus recomputed
length -- so nothing off the wire was trusted. Here it is not a safeguard bolted
on; it is the whole architecture.

The consequence worth putting in front of Return Infinity: **stateless
validated-output search is a workload class where the ISR bug provably does not
matter, today, unpatched.** That is a sellable sentence, and this experiment is
what earns the right to say it.

## Design

Four rungs. Each is a gate: it can stop the study, and stopping early is a
success.

### P0 -- price it, pick the instance, build the harness

Three deliverables, all cheap, none requiring BareMetal.

- **Pin the compute price.** The $0.005/core-hour rate below is **stipulated by
  this proposal, not measured.** No verified 2026 pricing for BareMetal Cloud
  or metal spot markets survived research. Pin it before spending.
- **Select instances.** All six instances Local-ILP touched have since been
  beaten -- scpm1 to 537, sorrell7 to -198, scpn2 to 489, supportcase22 to 111,
  cdc7-4-3-2 to -307 (Jan 2026) -- several by industrial groups including Huawei
  OptVerse. Choosing three open instances whose incumbent is not a
  commercially-resourced team is real work and it is the first task.
- **Harness.** Worker takes an instance and a seed, runs a bounded local
  search, reports its solution vector over outbound HTTPS. Collector
  revalidates. This is `cunningham.c` + `swarm_deploy.sh` with the payload
  swapped, plus the S3 register from the prior study demoted from a migration
  channel to a write-only results collector.

**Gate:** if no open instance both fits the memory envelope and has a
non-industrial incumbent, stop and report that.

### P1 -- the restart-length curve, on Linux

The science, and it is substrate-independent. Fix a total core-hour budget per
instance. Spend it at each granularity -- one long run, then 10, 10^2, 10^3,
10^4, 10^5 independent runs of proportionally shorter length -- on three
instances, with distinct seeds and no communication. Plot best objective
against restart length.

**Run this on Linux first and deliberately.** It is cheaper, it is easier to
debug, and the algorithmic question does not depend on the host. Running it on
BareMetal first would confound the finding with the 3.7x.

**Gate:** the pre-registered falsification above. If long trajectories win, the
study reports that and ends. Nothing further is worth spending.

#### P1 RESULT, 2026-09-04 -- partially run, and the gate is not cleanly passed

Run on **one** instance of the three (`pcb442`), 200,000 ant constructions per
arm, eight granularities, five replicates, combined by `min`. Full write-up and
data: [`../portfolio-curve/`](../portfolio-curve/).

The curve is a plateau and then a cliff. Everything from 1 to 200 workers is
statistically indistinguishable from a single long run -- the 40-worker arm is
nominally ahead -- and beyond that it degrades sharply: 400 workers 2.0 sigma
worse, 1,000 workers 5.9, 10,000 workers 10.5 and 1.65% off. The cliff is a
floor on task granularity, between 500 and 1,000 iterations, not a limit on
worker count.

**But read the falsification above as written.** It refuses the thesis if a
single long trajectory *"matches or beats"* the best portfolio granularity. It
matched -- 0.37 sigma apart. On the letter of the criterion, that is the
refusing condition, not the passing one.

Two things follow, and they should not be blurred together.

The criterion is **not satisfied on evidence**, because it requires two of
three instances and only one was run. P1 is incomplete. `kroA100` and `rat783`
are cheap and should be run before anything is concluded either way.

The criterion also looks **mis-specified in hindsight**, and that is recorded
as an observation rather than used as a rescue. "Matches" was written as a
failure because the proposal was thinking about search quality. But equal
quality at equal compute is exactly what makes a cheaper substrate worth having
-- the gain was never supposed to be better tours, it was supposed to be the
same tours bought on hardware that costs 20x less. A criterion that treats a
tie as a loss cannot see that.

The rule here is the one the proposal already states: a criterion adopted after
seeing results is not a criterion. So the original stands, the result is
recorded against it as ambiguous-tending-to-refused, and the fix is to finish
P1 on the other two instances rather than to rewrite the test.

#### P1 COMPLETE, 2026-09-04 -- the gate is not triggered

`kroA100` and `rat783` run to the same protocol. The criterion refuses the
thesis if a single long run matches or beats the best portfolio on **two of
three** instances:

| instance | single long run | best portfolio | matches or beats? |
|---|---:|---:|---|
| pcb442 | 51,478.0 | 51,473.4 | no |
| rat783 | 9,103.4 | 9,093.8 | no |
| kroA100 | 21,282.0 | 21,282.0 | yes, vacuously -- every arm hit the optimum |

One of three, and that one is saturated and carries no information. **The
pre-registered falsification is not triggered, and the thesis survives its own
test.**

It survives narrowly and should be quoted narrowly. The portfolio's wins on the
two informative instances are 0.37 and 0.80 sigma -- inside the noise. The
defensible claim is that **splitting the work across a few hundred workers
costs nothing**, not that it helps. That is still the claim the substrate
needs, since the gain was always meant to be cheaper hardware rather than
better tours, but it is a weaker sentence than "portfolios win" and should not
be rounded up to it.

The granularity floor is the finding the data actually supports strongly, and
it reproduces on both hard instances: free to a few hundred workers, collapsing
below roughly 500 iterations each. P2 is therefore live.

### P2 -- price the substrate

Only if P1 puts the minimum at short restarts. Run the winning granularity three
ways on one machine -- Linux processes, Linux microVMs, BareMetal unikernels --
at equal wall-clock, and measure achieved search throughput per dollar. This is
where the 31 ms boot and the 3.7x penalty meet each other, and it is a direct
head-to-head with a number at the end.

**Gate:** if Linux processes win outright, say so plainly. That is the likely
outcome at the current 3.7x, and predicting it here is deliberate.

### P3 -- the production run

Point the winning configuration at the selected open instances and let it run
the full budget. Submit any improvement to MIPLIB with the seed and the
solution vector, so the result is reproducible by a third party rather than
asserted.

## What the budget buys

Stipulated: **$250 at $0.005 per core-hour = 50,000 core-hours.**

Adjusted for the measured 3.7x, that is about **13,500 Linux-equivalent
core-hours**, or 48.6 million core-seconds of useful search. In the units this
problem is actually scored in:

| restart length | independent runs the budget buys |
|---:|---:|
| 300 s (Local-ILP's longest) | ~162,000 |
| 60 s | ~810,000 |
| 10 s (Local-ILP's shortest) | ~4,860,000 |

Against a published record set by runs of exactly this length, that is not a
rounding error -- it is between five and six orders of magnitude more attempts
than any single reported run. **This is the one target researched where the
budget is not the binding constraint.** Whether attempts convert into a record
is precisely what P1 measures and what nobody currently knows.

Two honest notes on the arithmetic. The $0.005 rate is stipulated; if it is
wrong by 10x, N changes by 10x and **no conclusion changes**, because P1 is a
budget-symmetric comparison -- every arm gets the same core-hours, so pricing
error cancels. And the 3.7x adjustment means $250 of BareMetal buys roughly
$68 of Linux compute, which is the penalty stated as a price rather than a
ratio.

## Metrics

| metric | definition |
|---|---|
| Solution quality | best objective value found, against MIPLIB's current best-known |
| Restart-length curve | best objective as a function of trajectory length at fixed total core-hours |
| Portfolio benefit | (best serial objective) - (best portfolio objective) at equal core-hours |
| Throughput per dollar | validated candidate solutions per dollar, by substrate |
| Boot overhead fraction | boot time / (boot + run) at the winning restart length |
| Fault cost | discarded-worker rate, and core-hours lost to it |

## Success criteria

A plain verdict on the restart-length question with the curve behind it. A
refusal is a successful study: *long serial trajectories beat portfolios at
equal compute, so ephemeral fan-out is the wrong shape for combinatorial
search* is a real finding, and it would redirect what this repo and Return
Infinity both claim the substrate is for.

Every headline number reproducible from a logged seed and a committed instance
file.

## Feasibility gates that could kill this early

Stated up front because each is capable of ending the study, and finding one
late is the expensive way.

- **Memory.** MIPLIB instances vary enormously. BareMetal's floor is 4 MiB and
  the working envelope here is 16 MiB. If no suitable open instance fits, the
  target is wrong even if the algorithm is right.
- **No libm.** This repo has no math library; prior work hand-rolled `exp` and
  `sqrt`. A pure integer/rational local search is plausible on this substrate
  and an LP-relaxation-based method is not. Local-ILP being a local search
  rather than an LP method is why it was chosen, but this needs confirming
  against the actual algorithm, not assumed.
- **No argv, and `clock()` does not advance.** Both found the hard way in prior
  work. Every worker parameter is compile-time, and a run cannot time itself --
  throughput must be measured from outside the VM.
- **Inbound serving fails at ~350 requests.** Established separately. The
  design is outbound-HTTPS-only for this reason.

## Out of scope

- **Beating Gurobi or CPLEX.** The comparison is portfolio against serial at
  equal compute, not against commercial solvers.
- **Proving optimality.** Upper bounds only. A claimed bound is not verifiable
  by a third party in seconds; a feasible solution is.
- **Patching the ISR.** That is Return Infinity's call. This study measures what
  runs correctly without it -- and argues the class where that is enough.
- **Isolating the 3.7x.** Named here as the highest-value open question, and
  explicitly not undertaken here. It needs the platform authors.

## What Return Infinity gets

1. **A named workload class**, with a measurement behind it rather than a
   claim -- or a documented refusal, which is worth nearly as much and costs
   less to obtain.
2. **A second, stronger fault-tolerance argument**: stateless search with
   externally revalidated output, where the 6.35e-7 register fault is
   structurally incapable of corrupting a result. Sellable today, unpatched.
3. **A concrete, prioritised bug**: the 3.7x compute penalty, with three causes
   already eliminated, presented as the one number standing between BareMetal
   and this workload class.
4. **A reproducible harness** that runs the same amalgamated C source on Linux
   and BareMetal, already demonstrated to produce bit-identical results across
   both substrates on a prior workload.
