# Ant Colony Optimization on a BareMetal Swarm — Study Design

**Date:** 2026-09-02
**Status:** Approved design, pending implementation plan
**Repo:** `unikernel-c`, branch `aco-swarm`

## Purpose

This repo has established that BareMetal runs real compute — Monte Carlo, MCMC,
primality, TLS — inside 16 MiB with no OS. It has also established, by
controlled experiment, that the platform **computes incorrectly** about once in
1.6 million operations: an interrupt landing inside a multi-instruction sequence
corrupts a live register (`docs/arithmetic-fault/`). The Cunningham swarm has to
caveat its own coverage because of it (`docs/swarm-run-02.md`).

That defect is treated so far as a liability to be worked around. This study
asks a different question:

> **Is there a class of workload for which that defect does not matter — and
> can BareMetal run it correctly today, unpatched?**

The claim under test is that Ant Colony Optimization is exactly that class, for
a structural reason rather than a lucky one. ACO is a stochastic search whose
state is a pheromone matrix under continuous exponential decay. A corrupted
pheromone write perturbs one selection probability; the perturbation is absorbed
by stochastic construction and erased by evaporation within a few iterations.
**The algorithm's forgetting mechanism is an error-correction mechanism.**

The contrast is the point. Prime search is fault-*intolerant*: one corrupted
modulo silently drops a find, unrecoverably, and the run must caveat its
coverage. ACO is fault-*tolerant*: the same corruption rate leaves the answer
unchanged. If that holds, it names the workload class BareMetal should be sold
for before the ISR is fixed — and it is falsifiable, which is what makes it
worth measuring.

## Established before design

Five facts from this repo constrain everything below. None is re-derived.

**1. The arithmetic fault is real, measured, and root-caused.**
`mod_bad=127` in 200,000,000 operations (6.35e-7), zero with `cli`, zero with an
atomic `divq`. It is a register-preservation bug in the interrupt path, proven
by controlled comparison, not inferred. `docs/arithmetic-fault/`

**2. The fault was only ever demonstrated on integer GP registers.**
Whether the ISR also fails to preserve XMM state is **untested**. ACO is
float-heavy where every prior workload here was integer-heavy. This is a gap in
the existing finding, not merely a risk to this study.

**3. There is no math library.** `ising.c` hand-rolls `exp` and `sqrt`. Any
design requiring `pow` is a design requiring new numerics.

**4. Randomness is solved and proven.** `RDSEED`/`RDRAND` pass through
Firecracker; the conditioned generator passes `ent`, FIPS 140-2 and dieharder.
`docs/trng/`

**5. The deployment pattern is outbound-HTTPS-only, compile-time-configured,
and stateless across restart.** `cunningham.c` + `scripts/swarm_deploy.sh`.
Inbound serving is separately known to fail at ~350 requests. Workers hold no
state across a reboot.

## Algorithm

**MAX–MIN Ant System** (Stützle & Hoos), not the original Ant System. MMAS
clamps pheromone into `[τ_min, τ_max]` and lets only the best ant deposit, which
is what prevents premature convergence in practice. It is also, conveniently,
the variant that needs no transcendental functions.

Every numeric choice below exists to stay inside constraint 3 — **no libm at
all**:

| Quantity | Form used | Why it needs no libm |
|---|---|---|
| Selection weight | `τ_ij / d_ij^β`, **β = 2** | η = 1/d, so `τ·η^β` is one divide by an integer square — no `pow` |
| α | fixed at 1 | `τ^1` is τ |
| τ_max | `1 / (ρ · L_best)` | one divide |
| τ_min | `τ_max / (2n)` | the standard simplification; the textbook form needs an n-th root |
| Distances | EUC_2D, hand-rolled `sqrt` from `ising.c` | already solved in-repo |
| Roulette wheel | sum weights, draw, walk | multiplies and compares only |

β is fixed at 2 for every reported run. R1 runs β = 3 once, on `kroA100`, as a
sensitivity check only; it is recorded and never used to select the headline
configuration. Instructions and parameters are frozen before any measured run
and are not tuned against results.

**RNG:** seeded xoshiro256++ in the hot loop. **Not** `RDRAND` — it costs
hundreds of cycles per draw and is irreproducible. `RDSEED` is used once, to
mint the seed, which is logged. Same-seed divergence between two runs then
becomes a *measurement of the platform* with a known mechanism, rather than
noise of unknown origin.

**Candidate lists:** 20 nearest neighbours per city, precomputed. Standard MMAS
practice and essential to iteration rate; 783 × 20 × 2 B = 31 KB, negligible.

## Instances and answer keys

TSPLIB, because it supplies published optima — the same answer-key discipline
that made the Sage reasoning study checkable.

| Instance | n | Published optimum | Role |
|---|---:|---:|---|
| `berlin52` | 52 | 7,542 | smoke test, sub-second |
| `kroA100` | 100 | 21,282 | R1 development |
| `pcb442` | 442 | 50,778 | main measurement |
| `rat783` | 783 | 8,806 | memory-ceiling case |

Quality is reported as **gap to optimum**, `(L − L_opt) / L_opt`, never as raw
tour length.

### Memory budget

The binding constraint is the 16 MiB instance beside lwIP, mbedTLS, curl and a
CA bundle. At n = 783:

- pheromone, float32 `n²` — 2.45 MB
- distances, **computed on the fly** with the hand-rolled `sqrt` — 0 MB
  (a cached `uint16` matrix would be 1.23 MB; adopt only if measured
  iteration rate demands it, and record the decision)
- coordinates, candidate lists, ant tours — under 100 KB

Arrays are sized at compile time and the program prints its own high-water
mark, reusing the `agent_static.c` idiom, so the budget is evidenced rather
than assumed.

## Migration

Islands must exchange best-so-far tours using **outbound HTTPS only**
(constraint 5). Two platform rules rule out the obvious Telegram designs:

- **Bots cannot see messages from other bots**, "regardless of mode" (Bot FAQ).
  One-bot-per-worker therefore cannot pass tours between workers at all.
- **`getUpdates` is single-consumer per token.** Multiple pollers produce
  `409 Conflict: terminated by other getUpdates request` and steal each other's
  updates.

### The pinned message as a shared register

All workers share **one** bot token — so there is no bot-to-bot boundary, they
are the same bot — and migration uses `getChat`, which has no offset and no
long poll:

- **Publish:** on beating the known global best, `sendMessage` the tour, then
  `pinChatMessage` on the returned message id.
- **Read:** `getChat` returns `pinned_message`, a full `Message` object whose
  text carries the tour.

Single-writer-wins. Races are benign: migration is advisory, and a lost update
delays a good tour by one interval. Encoding is base64 of `uint16` city
indices — `rat783` is 1,566 B → 2,088 chars, inside the 4,096-char message
limit with room for a header carrying instance name, tour length, and the
publishing worker's id.

Publishes are naturally self-limiting, because best-so-far improvements get
rarer as the search converges. A minimum publish interval per worker guards the
early phase, when they do not.

### Two backends, one interface

```c
int  register_read (char *buf, size_t cap);   /* current global best, or empty */
int  register_write(const char *payload);     /* publish + claim              */
```

- **Telegram backend** — the 4-island BareMetal Cloud run.
- **Local-endpoint backend** — the Firecracker sweep. Thirty-two islands would
  exceed Telegram's ~20 messages/minute per-chat limit; four will not.

Chosen at compile time, like every other setting on this platform.

## Swarm width

`supervisor.c:41` records the cloud's per-user cap: **`MAX_INSTANCES 4`**. That
splits the work across two substrates.

- **BareMetal Cloud, 4 islands** — the flagship run, on the deployment path
  already proven by swarm runs 01 and 02.
- **Firecracker on one AWS `.metal` host, up to 32 islands** — the island-count
  sweep, using the `docs/nanos-vs-baremetal/` harness. Same guest, same defect,
  full control, no cap.

A single global-best register is **all-to-best** coupling — the strongest
migration topology available. Beyond some island count it collapses diversity:
every island descends into one basin and the benefit of having islands
disappears. At 4 this is invisible; across the sweep it is a curve, and
*where sharing begins to hurt* is a more interesting finding than *sharing
helps*.

## Rungs

Each rung is independently reportable. A rung that fails does not invalidate
the ones before it.

### R0 — `floatscope`, and a register probe

Two things that must precede any ACO code.

**`floatscope.c`** — the FP sibling of `faultscope.c`, in the same format:
a fixed floating-point chain repeated 200,000,000 times, mismatches counted,
with a `cli` variant as the control. It closes the gap named in constraint 2
and establishes ACO's noise floor on this platform. It has standalone value
whichever way it comes out: a clean result narrows the existing bug report, a
dirty one materially extends it.

**Telegram register probe** — confirm live, before building on it: the admin
right `pinChatMessage` actually requires in the chosen chat type, the real
per-chat rate limit, and that `getChat` returns `pinned_message` text as
documented. Cheap, and it de-risks the whole migration design.

### R1 — one colony, one host

Single-island MMAS on `kroA100` and `pcb442`, Linux versus BareMetal under
Firecracker on the same machine, reusing the `nanos-vs-baremetal` harness.

Reports: gap to optimum at fixed iteration counts; iterations/second;
**same-seed divergence rate** between repeated identical runs.

Linux is the correctness oracle — the identical binary, identical seed, must
reach the identical tour there. Any divergence on BareMetal alone is
platform-attributable.

### R2 — swarm, no migration

Four independent colonies with distinct seeds and no communication: pure
multi-start. This is the **control**, and it is the existing cunningham deploy
pattern with the workload swapped, so it costs almost no new infrastructure.

Reports: best-of-4 gap versus single-island gap at equal wall-clock.

### R3 — swarm with migration

R2 plus the pinned register. Reports the same metrics against R2 directly:
does sharing beat independent restarts, at equal wall-clock and equal island
count? Then the Firecracker sweep across 2, 4, 8, 16, 32 islands, with and
without migration, for the diversity-collapse curve.

### R4 — the fault-tolerance claim

The finding the rest exists to earn. On Linux, inject corruption into pheromone
writes at the measured platform rate (6.35e-7), then at 10×, 100×, 1000×, and
measure gap to optimum against an uncorrupted control.

Prediction: indistinguishable from control at the platform rate, and
degrading only far above it. Paired against the same injection applied to the
Cunningham search, where a single corrupted modulo silently drops a find, this
is a direct, quantitative statement of which workloads this platform can carry
today.

Stated so it can fail: **if injected faults at 1000× the platform rate measurably
worsen the gap to optimum, the fault-tolerance claim is refused** and the study
reports that instead.

## Metrics

| Metric | Definition |
|---|---|
| Gap to optimum | `(L − L_opt) / L_opt` against the published TSPLIB value |
| Iteration rate | constructed tours per second per island |
| Same-seed divergence | fraction of identical-seed run pairs reaching different tours |
| Quality vs wall-clock | gap as a function of elapsed seconds, not iterations |
| Migration benefit | R3 gap − R2 gap at equal wall-clock and island count |
| Fault sensitivity | gap as a function of injected corruption rate |

Wall-clock, not iterations, is the comparison basis wherever substrates or
island counts differ — iteration counts are not comparable across them.

## Success criteria

A plain verdict on the central question with the number behind it. A refusal is
a successful study: *ACO is as fault-sensitive as prime search, and BareMetal
cannot carry it either* is a real and publishable finding, and it would redirect
the platform advice this repo gives.

Every headline number must be reproducible from a logged seed and a committed
instance file.

## Out of scope

- **Beating LKH.** MMAS on `pcb442` will not match Lin–Kernighan–Helsgaun and
  is not trying to. The comparison here is BareMetal against Linux on the same
  algorithm, and ACO against prime search on fault tolerance.
- **Patching BareMetal's ISR.** That is Return Infinity's fix. This study
  measures what runs correctly without it.
- **Dynamic TSP.** A perturb-mid-run rung is a natural successor once static
  results exist; it is not in this study.
- **Sage.** ACO needs 10⁶–10⁸ component evaluations per solve against a
  5,000-unit monthly budget; it is off by four orders of magnitude. Not used.
- **Latency.** Measured already, twice, and not this study's subject.
