# Swarm run 02 — corrected partition

**26 August 2026 · BareMetal Cloud · 3 × (1 vCPU, 16 MiB) unikernel instances**

Run 02 exists to test the partition fix from run 01, where two of three workers
sat in residue classes that cannot produce a Cunningham chain. Candidates are now
`p = 6k − 1` with `k` split across workers, so every worker is in the productive
class `p ≡ 5 (mod 6)`.

Resumed from `START_K = 1,866,666,666` (p ≈ 1.12×10¹⁰), where run 01 left off, so
no range was re-searched. Announce floor raised to 9.

## Did the fix work

Yes, and the evidence is the balance between workers.

| | Run 01 (broken) | Run 02 (fixed) |
|---|---|---|
| Workers finding chains | 1 of 3 | **3 of 3** |
| Best chain per worker | 9 / 1 / 0 | **8 / 8 / 8** |
| Position spread across workers | 2100% | **0.96%** |

In run 01, worker 2 raced 22× ahead of the others because every one of its
candidates was divisible by 3 and died at the first trial division — fast, and
worthless. In run 02 all three advance together through a shared `k` space, and
all three report the same best chain length, which is what a correctly
partitioned swarm looks like.

## Result

| Metric | Value |
|---|---|
| Range covered | 11,200,000,000 → 12,876,963,347 |
| Numbers searched | 1,676,963,347 |
| Chains of length ≥ 9 | 0 |
| Longest chain seen | 8 (by all three workers) |
| HTTPS posts | 9 (3 per worker), 0 failures |

Zero length-9 finds is the expected outcome, not a fault. Run 01 found 6 chains
of length 9 across 1.12×10¹⁰, a density of about one per 1.9×10⁹. This run
covered 1.68×10⁹, so the expectation was ≈0.9 finds; observing 0 is unremarkable
for a Poisson process at that rate.

## Measured throughput, and a correction

Two watcher samples 622 s apart:

```
14:43:02  min = 11,976,963,347
14:53:24  min = 12,876,963,347
```

That is **1,446,945 numbers/second** across the swarm — about 241,000
candidates/s in total, or **~80,000 candidates/s per worker**.

An earlier estimate of ~400,000 candidates/s per worker was wrong. It came from
sampling `searched to` twice about two minutes apart, but that field only updates
once per heartbeat (every 50 million candidates), so the sample was quantised and
happened to span a boundary. The projection built on it — "10¹¹ in about 3.4
hours" — was consequently ~5× optimistic. At the properly measured rate, reaching
10¹¹ from here would take **≈17 hours**.

The lesson is a general one for this kind of workload: a counter that only
advances at coarse intervals cannot be used to measure a rate over a window
comparable to that interval.

## Caveats

Coverage is not exhaustive. The BareMetal runtime does not compute
deterministically for this workload (`docs/technical-report.html`, §4), so the
search silently misses some chains in any range it passes over. It does not
invent them: every find in run 01 was independently re-verified.

Workers hold no state across a restart, so any reboot returns that worker to
`START_K`.

## Teardown

All three instances stopped and deleted at the end of the run. Images remain in
the account and can be redeployed with `scripts/swarm_deploy.sh`.
