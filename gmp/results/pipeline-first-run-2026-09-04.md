# First end-to-end run of the chain hunt

Sieve and PRP stage joined up and run against the real k=6 target for the first
time, on the development Mac (arm64, no metal host needed for either stage).

## Correctness first

Both stages are checked against answers known independently of this code.

`cc_sieve --selftest` brute-forces a small parameter set in both directions:

```
p#=13# k=3 j=1 m in [1,20001) depth=5000
sieve kept 785, brute force kept 785
survivors the sieve wrongly killed: 0
composites the sieve wrongly kept:  0
```

`cc_hunt --selftest` uses classical chains small enough to verify by hand:

```
m=3    len=5   2 5 11 23 47
m=90   len=6   89 179 359 719 1439 2879
m=7    len=0   first term is 6, composite
m=6    len=4   5 11 23 47, then 95 = 5*19
9 correctly rejected as composite
```

The length-6 case matters: it is the exact shape being hunted at 1000 digits,
only small enough to check by eye. Two of these expectations were wrong when
first written and the selftest caught both -- which is the point of having it.

## The run

```
200,000 multipliers, primorial 2357#, k=6, j=1, depth 1e9, ~1000 digits

sieve   523 survivors        rate 2.6e-3
hunt    523 tested in 4.33s  8.29 ms per multiplier
HUNT_HIST  0:514  1:9  2:0  3:0  4:0  5:0  6:0
```

Two things line up with theory rather than with each other, which is the useful
kind of agreement:

- **Survival rate 2.6e-3 against a predicted 2.8e-3.** The Mertens estimate
  `(ln p / ln P)^k` for depth 1e9 is in the table in `cc_sieve.c`'s header. It
  was written before this ran.
- **8.29 ms per multiplier**, against 10.517 ms measured for a single PRP test
  on the c5.metal. Slightly faster here because this is an M-series Mac, and
  because most multipliers die on the first term rather than costing k tests.

The histogram is the real diagnostic. 514 of 523 died on term 0 and 9 reached
one term; nothing reached two. A fall of roughly 60x per term is what ~1000
digit candidates should do after sieving, and it is why the cost model can
assume barely more than one PRP test per multiplier.

## A measurement that corrected itself

The first read of this run said the sieve was 90% of the cost -- 38 s of sieving
against 4.3 s of PRP -- which would have argued for a shallower depth and a
different split between the host and the workers.

That was an artifact of the span. Sieving to depth 1e9 means enumerating
5.76e7 primes, and that cost is fixed, not per-multiplier. Running it over ten
times as many multipliers confirms it:

| multipliers | sieve time |
|---:|---:|
| 200,000 | 38.0 s |
| 2,000,000 | 39.2 s |

Ten times the work for 3% more time. At 2e6 the two stages are about even, and
beyond that the PRP stage dominates as the cost model assumes. `cc_sieve.c`'s
header says exactly this -- "the per-prime setup cost is only worth paying if it
is amortised over a large span of m" -- so the correction was available before
the mistake was made.

## Where this leaves the deployment

Nothing here needed a metal host: both stages are ordinary userland programs.
Putting the PRP stage on BareMetal Cloud does need one, because building a
unikernel image requires the BareMetal-AppPort toolchain on Linux x86-64, and
the c5.metal used for the ring3 measurements has been terminated. That build
host is the only thing standing between this and the four cloud instances.

## The search is now running

`gmp/scripts/hunt-local.sh` runs the hunt in resumable chunks on an ordinary
host. State is one integer -- the next multiplier -- so a chunk that is killed
loses at most that chunk, which is the same contract the cloud workers will
have. It refuses to start if either selftest fails, because a hunt that sieves
wrongly looks exactly like a hunt that is merely unlucky, for as long as it runs.

First chunk, on the development Mac:

```
m=[1,2000001)  survivors=5495  best=2 at m=870805  84 s
HUNT_HIST  0:5415  1:79  2:1  3:0  4:0  5:0  6:0
```

The falloff is 5415 -> 79 -> 1, about 68x per term, which is what ~1000-digit
candidates should give and matches the 60x the first run suggested.

Throughput is roughly 2e6 multipliers per 84 s, or 2.1e9 per day on one core.
Against the ~2.8e12 multipliers implied by 7.3e9 PRP tests at a 2.6e-3 survival
rate, that is about 1,360 core-days -- consistent with the 21,326 core-hour
projection, and a restatement of why the instance cap rather than the price is
what decides the calendar.

Stop it with `kill` on the driver; restart picks up from `results/hunt-local/next_m`.
