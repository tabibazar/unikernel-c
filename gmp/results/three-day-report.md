# Cunningham chain hunt: three-day report

**4–7 September 2026.** Target: a Cunningham chain of the first kind of length
6, of the form `N_i = m · 2357# · 2^(j+i) − 1`, ~1011 digits.

**Result: no chain of length 6, and none expected.** The longest found is 4
terms. That is the arithmetic working, not the search failing, and the reasons
are below.

## What was examined

| | multipliers | survivors PRP-tested |
|---|---:|---:|
| local hunt (Mac, 1 core) | 6,176,000,000 | 17,375,219 |
| BareMetal Cloud | 3,780,000,000 | 10,631,976 |
| **total** | **9,956,000,000** | **28,007,195** |

Throughput: **2.106 × 10⁹ multipliers per core-day**, from 70.4 local core-hours
and 76.3 cloud instance-hours.

**That is 0.356% of the ~2.8 × 10¹² multipliers the projection implies.** The
remaining work is about **281×** what has been done. Three cores for three days
was never going to close that, and the plan never claimed it would.

## Chain-length histogram

| terms | count |
|---:|---:|
| 0 | 27,561,760 |
| 1 | 438,390 |
| 2 | 6,943 |
| 3 | 101 |
| **4** | **1** |
| 5 | 0 |
| 6 | 0 |

Falloff per term: **62.9×, 63.1×, 68.7×** — near-constant across four terms and
five orders of magnitude. At a geometric 64.9× the expected count of 4-chains in
this much work is **1.6**; one was found. The expected count of 6-chains is
**3.7 × 10⁻⁴**; none was found. The search is behaving exactly as the theory says
it should, which is the useful result here.

### The one 4-chain

```
m = 1,854,182,119
N_0 .. N_3 = m · 2357# · 2^(1+i) − 1,  1010 digits each,  all probable prime
N_4 composite — the chain genuinely stops at four
```

Independently re-verified at **256 Miller-Rabin reps** by a separate program
that rebuilds the primorial and constructs each term directly, sharing no code
with the hunt. Still probable primes, and at four terms there is nothing worth
claiming.

## Survival rate: no drift

| | survival |
|---|---:|
| Mertens prediction `(ln p / ln P)^k`, written before any run | 2.8e-3 |
| measured 2026-09-05 | 2.837e-3 |
| measured over the full three days | **2.8131e-3** |

**0.8% from the earlier measurement.** A drift here would have meant the sieve or
the arithmetic was wrong; there isn't one.

## Cloud

- **36 of 38 slices finished**, 30 redeploys used of a 60 budget
- **76.3 instance-hours, $0.38** of the $23 credit
- Coverage: **0 holes.** 3,780,000,000 multipliers claimed and 3,780,000,000
  actually searched.

**The tender did not run unattended without intervention.** It needed rescuing
three times, and each failure is worth more than the compute it cost:

1. **Image-quota deadlock** (~4 h down). BareMetal Cloud caps images at 10, which
   `/api/limits` does not report. The tender retired the old image only after a
   successful upload — which is crash-safe and quota-fatal: the upload failed
   because the quota was full, so the old image was never retired, so the next
   upload failed identically.
2. **Reboot took Docker with it** (~1.5 h down). The tender harvested the stopped
   worker, deleted it, then could not build a replacement — leaving nothing
   running. It now builds *before* destroying, and treats a missing worker as a
   reason to deploy rather than a reason to idle.
3. **Coverage holes** — the serious one, below.

## The failure that mattered

The cloud's progress counter advanced when a slice was **deployed**, not when it
**finished**. Two things followed, and neither raised an error:

- a manual redeploy read a counter that had already moved, stepping over
  **315 million** multipliers;
- two slices killed mid-flight during the scale-down had their ranges marked
  consumed at 18% and 16% complete — another **210 million**.

**525 million multipliers, 15.2% of the claimed span, were never examined**, and
the report you are reading would have said "we searched 100e9 to 103.5e9 and
found nothing". That sentence would have been false.

The local hunt had **zero** holes over 3,088 chunks — same job, same shape of
counter, and the only difference is that it advances after a chunk *completes*.

`coverage.py` now derives coverage from `WORKER_DONE` lines rather than from any
counter, queues what is missing, and the tender drains that queue before
extending the frontier. All holes are closed as of this report.

## Return Infinity

Ian Seyler answered the throughput question directly on 5 September:

> *"In the alpha of BareMetal Cloud all instances are sharing the same vCPU. CPU
> intensive workloads will certainly suffer until our infrastructure scales out
> in the next version."*

Measured against that: our worker ran at **58.2 ms per candidate with three
workers up and 19.2 ms alone — 3.03×**. Aggregate throughput was 51.55
candidates/sec at three workers against 52.08 at one, **identical within 1%**.
Adding instances adds no throughput at all.

Consequences:

- We run **one** instance, not three: same work, a third of the cost.
- The reported 7.2× gap was mostly our own contention. A single tenant is **2.4×**
  slower than an M-series core, which for a shared alpha vCPU is respectable.
- **The instance-cap ask is parked**, and correctly — more instances cannot help a
  CPU-bound job while they share a vCPU.

**No reply on the cap itself, and none needed while the platform is
single-vCPU.** A follow-up carrying the contention measurement is drafted and
unsent, awaiting a decision.

## What this actually established

Not a record, and not progress toward one worth mentioning — 0.356% of a search
is not a foothold.

What it did establish:

- **The deployment path works.** A 3.8 MB unikernel runs in 16 MiB with GMP's
  x86-64 assembly intact at the high canonical load address, and every worker
  passes a known-answer check on the target hardware before reporting anything.
- **The arithmetic is right.** Survival within 0.8% of a prediction made before
  the first run, and a chain-length falloff constant to within 10% across four
  terms.
- **The harness is honest.** It found its own 15% coverage hole, and now cannot
  repeat it.
- **The platform's real limits**, measured rather than assumed: shared vCPU,
  10-image cap, and a 16 MiB ceiling that bounds how much work one image can
  carry.

The negative result is the expected one. The value is that it is a *trustworthy*
negative result, which it would not have been three days ago.
