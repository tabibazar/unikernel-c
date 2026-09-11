# Thread-scaling calibration on AWS metal spot

Run on a **c6i.metal spot in ap-south-1c at $0.6316/hr**, 17 minutes, **$0.18**.
Xeon Platinum 8375C, 2 sockets x 32 cores, 2 threads/core — 64 physical cores,
128 logical. Not a c5; spot, not on-demand.

Same `cc_hunt` binary, same 5,715-survivor slice given to every worker, so the
only variable is how many run at once.

| threads | elapsed | candidates | ms each | throughput | scaling |
|---:|---:|---:|---:|---:|---:|
| 1 | 57.8 s | 5,715 | 10.11 | 98.9/s | 1.0x |
| 16 | 58.4 s | 91,440 | 0.64 | 1,566.7/s | 15.8x |
| 32 | 60.0 s | 182,880 | 0.33 | 3,047.6/s | 30.8x |
| 64 | 62.0 s | 365,760 | 0.17 | 5,902.1/s | **59.7x** |
| 96 | 85.3 s | 548,640 | 0.16 | 6,429.7/s | 65.0x |
| 128 | 107.2 s | 731,520 | 0.15 | 6,824.2/s | **69.0x** |

## Two things worth keeping

**Scaling is near-linear to 64 threads — 59.7x on 64 physical cores.** GMP
big-integer work has no shared state and a working set that fits in cache, so
nothing contends.

**Hyperthreads are nearly worthless here: 15.6% for twice the threads.** Two
threads per core saturate the same ALUs. Useful to know before paying for vCPU
counts: the 128 in "128 vCPU" buys 69x, not 128x.

## Against the estimate

The model predicted 74.7x from `physical cores x (clock / 3.0 GHz)`. Measured
69.0x, so the estimate was **8% optimistic** — close enough that the cost table
stands with a small correction upward.

| coverage | P(find) | instance-hours | cost |
|---:|---:|---:|---:|
| 1x | 63.2% | 297 | $188 |
| 1.5x | 77.7% | 446 | $282 |
| 2x | 86.5% | 594 | $375 |
| 3x | **95.0%** | 891 | **$563** |

P(at least one chain) is Poisson, `1 - exp(-lambda)`. An earlier note in
conversation said $175 bought a 95% chance; that was wrong — $188 buys **1x
expectation, 63%**. 95% is 3x that.

## Capacity is the real constraint, not price

The first launch **failed outright**: `InsufficientInstanceCapacity` for
m6i.metal in us-east-2c, the cheapest AZ. And spot prices varied **2.2x between
AZs of one region** — us-east-2c $0.614, us-east-2a $0.980, us-east-2b $1.351.

So a 297-hour job will be interrupted, and pinning to the cheap AZ is what makes
the price real rather than nominal. Work is checkpointed per slice, so a reclaim
costs one slice — but the tender would need to handle reclaim, which it has
never been tested against.

## Before spending anything

Check whether PrimeGrid or another BOINC project is already sweeping this space.
Sophie Germain primes are Cunningham chains of length 2 and are an active
subproject; if longer chains are also being searched by thousands of volunteers,
the density model's implied difficulty is optimistic and collaborating beats
competing. **This has not been verified** — it needs a literature and project
check, and it is the single fact that could invalidate the plan.
