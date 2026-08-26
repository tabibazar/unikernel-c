# Swarm run 01 — Cunningham chains

**26 August 2026 · BareMetal Cloud · 3 × (1 vCPU, 16 MiB) unikernel instances**

Superseded by run 02. Kept because the results are real and the failure is
instructive.

## Result

56 chains found, all by worker 0. Every length-9 find was re-verified
independently (Python, deterministic Miller–Rabin) after capture.

| Chain length | Found |
|---|---|
| 9 | 6 |
| 8 | 50 |

### Chains of length 9

| Start | Verified |
|---|---|
| 198,479,579 | ✓ |
| 305,192,579 | ✓ |
| 2,400,025,739 | ✓ |
| 7,606,886,429 | ✓ |
| 7,755,909,149 | ✓ |
| 10,378,311,899 | ✓ |

The first in full — each term double the previous plus one, all nine prime:

```
198,479,579 → 396,959,159 → 793,918,319 → 1,587,836,639 → 3,175,673,279
→ 6,351,346,559 → 12,702,693,119 → 25,405,386,239 → 50,810,772,479
```

Chains of length 8: 50 finds between 171,729,539 and 11,008,549,619.

## Final positions

| Worker | Searched to | Finds | p (mod 6) |
|---|---|---|---|
| w0 | 11,199,999,995 | 56 | **5** |
| w1 | 12,099,999,997 | 0 | 1 |
| w2 | 246,999,999,999 | 0 | 3 |

## Why two thirds of the swarm did nothing

The partitioning was wrong. Workers split the *odd numbers* by index modulo the
swarm size, which — for any start value — pins each worker to a fixed residue
class modulo 6. Two of the three classes cannot produce Cunningham chains at
all:

- **w1 got p ≡ 1 (mod 6).** Then `2p+1 ≡ 3 (mod 6)`, divisible by 3, so no
  candidate can ever extend to a chain of length 2. It found primes, never chains.
- **w2 got p ≡ 3 (mod 6).** Every candidate is divisible by 3, so none is prime.
  This is why it ran 22× "faster": each candidate died at the first trial
  division. It searched a quarter of a trillion numbers and found nothing.
- **w0 got p ≡ 5 (mod 6)**, the only class that can start a chain — which is
  where all 56 results came from.

The effective parallelism was 1, not 3. Fixed in run 02 by enumerating
`p = 6k − 1` and partitioning on `k`, so every worker sits in the productive
class and no test is spent on a residue that cannot yield a chain.

## Caveats on coverage

"Searched to N" is not a claim of exhaustive coverage of the range. The
BareMetal runtime does not compute deterministically for this workload (see
`docs/technical-report.html`, §4): identical passes over an identical range
return different prime counts, always undercounts, at roughly 2×10⁻⁴ on cloud
hardware. The search therefore **misses** some chains it passes over. It cannot
invent them — every find above was verified — so the results are sound; the
coverage is not complete.

Workers also hold no state across a restart. Any reboot, including a repair by
the supervising agent, restarts that worker's search from the beginning.
