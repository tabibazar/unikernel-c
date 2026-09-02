# Does the engine optimise? Measured against published optima

Unit tests cannot establish that an optimiser optimises. This is the gate the
plan set before any result was seen, and the result it actually produced.

> **Status.** The table below is the *construction-only* engine — the
> configuration the pre-set gate was written against, and the one whose miss is
> reported here unedited. 2-opt local search was adopted afterwards; the closed
> decision and the numbers it produced are at the [end of this
> document](#local-search-decision-taken), raw sweep in
> [`VALIDATION-ls.txt`](VALIDATION-ls.txt). Everything from R1 onward runs with
> local search on.

Host: Apple arm64, clang `-O2 -std=c99`, one core, distances on the fly
(`ACO_DIST_CACHE=0`, the configuration these runs used). Parameters frozen by
the spec: alpha 1, beta 2, rho 0.02, 25 ants, candidate list 20.

| instance | n | optimum | seeds | at optimum | median gap | best gap | iters/s | valid |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| berlin52 | 52 | 7,542 | 10 | **10** | 0 | 0 | 6,189 | all |
| kroA100 | 100 | 21,282 | 10 | 0 | 0.45% | 0.06% | 3,179 | all |
| pcb442 | 442 | 50,778 | 10 | 0 | 14.10% | 13.25% | 473 | all |
| rat783 | 783 | 8,806 | 5 | 0 | 11.31% | 9.70% | 227 | all |

## Verdict against the pre-set gate

| criterion | required | measured | |
|---|---|---|---|
| every tour a valid permutation | all | all 35 runs | **pass** |
| berlin52 at optimum | >= 8/10 | 10/10 | **pass** |
| kroA100 at optimum | >= 5/10 | 0/10 | **fail** |
| kroA100 worst gap | < 2% | 0.45% | pass |
| pcb442 median gap | < 5% | 14.10% | **fail** |

**The engine is correct and under-powered.** Correct: 10/10 on berlin52, every
tour a valid permutation, identical seeds reproducing bit-exactly. Under-powered:
the gap grows with n, which is the signature of construction-only MMAS.

The cause is not a mystery and it is not the frozen parameters. Canonical MMAS
for TSP (Stuetzle & Hoos) runs a **2-opt or 3-opt local search** on every
constructed tour; this implementation has none, because the spec never named it.
Published MMAS-without-local-search results sit in this range.

Two observations support that reading rather than a defect hunt:

- **Nine of ten kroA100 seeds converged to the identical tour** (21,379). Deep
  convergence to one local optimum, not a search still making progress.
- **Doubling the iteration rate barely moved the gap.** With the distance cache
  (below), pcb442 got 2x the iterations in the same 20 s and improved only
  15.45% -> 14.49%. The ceiling is search quality, not search quantity.

No parameter was tuned to chase the gate. The miss is reported, per the plan.

## Why the hand-rolled sqrt cannot distort a distance

`euc2d` is `nint(sqrt(dx^2+dy^2))`, and it is the one place where the
hand-rolled root could have changed a result. It cannot, and the reason is
arithmetic rather than testing.

Coordinates are integers, so `m = dx^2+dy^2` is a non-negative integer. Rounding
flips only if `sqrt(m)` sits near a half-integer `t = k + 1/2`. But
`t^2 = k^2 + k + 1/4`, and `k^2 + k` is an integer, so `|m - t^2| >= 1/4` always.
Hence

    |sqrt(m) - t| = |m - t^2| / (sqrt(m) + t) >= 1 / (8*sqrt(m))

At the largest distance in these instances (~5,700) that floor is about
2.2e-5. One ulp at that magnitude is about 9e-13. `a_sqrt` would have to be
wrong by more than ten million ulps to move a single rounded distance.

So the distances are TSPLIB-exact whatever the root's last bits do — which is
also why berlin52's published optimum of 7,542 is reproduced exactly, rather
than approached.

## Decision: adopt the distance cache

The spec permitted a `uint16` distance matrix "only if measured iteration rate
demands it, and record the decision". Measured, 20 s, seed 1:

| instance | on the fly | cached | static footprint |
|---|---:|---:|---|
| pcb442 | 9,372 iters | 18,636 iters | 0.77 -> 1.14 MB |
| rat783 | 4,424 iters | 8,644 iters | 2.38 -> 3.55 MB |

Roughly 2x for about a megabyte, against a 16 MiB instance. **Adopted**
(`ACO_DIST_CACHE` now defaults to 1).

Verified a pure optimisation before adoption: same seed and same iteration
count give a bit-identical tour with the cache on and off, across two instances
and three seeds — while different seeds give different tours, which is what
proves the comparison exercised the search rather than a seed-independent
starting tour. (A first attempt at this check was vacuous: at 200 iterations
the search had not yet beaten the nearest-neighbour start, which is
seed-independent, so every run returned the same number.)

## Local search: the decision as it stood

Adding 2-opt is a **missing standard component** of MMAS-for-TSP, not a tuned
parameter, and it would close most of the gap on pcb442 and rat783.

It is also probably not load-bearing for this study's claim. R4 compares
corrupted against uncorrupted runs of the *same* configuration; a stable 14%
baseline serves that comparison as well as a 2% one would. Beating LKH is
explicitly out of scope. The decision is recorded here rather than taken
quietly, because taking it after seeing these numbers is exactly the move the
spec's no-tuning rule exists to prevent.

## Local search: decision taken

Adopted. `ACO_LOCAL_SEARCH` defaults to 1, `validate.sh` pins it and asserts
the built binary really reports `ls=1`, and every rung from
[`../docs/aco-r1/`](../docs/aco-r1/) onward ran in this configuration.

Same script, same pre-set gate, same frozen parameters, `CACHE=1 LS=1`. Raw
output: [`VALIDATION-ls.txt`](VALIDATION-ls.txt) (the construction-only sweep
above is [`VALIDATION.txt`](VALIDATION.txt)).

| instance | n | optimum | seeds | at optimum | median gap | best gap | iters/s | valid |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| berlin52 | 52 | 7,542 | 10 | **10** | 0 | 0 | 11,283 | all |
| kroA100 | 100 | 21,282 | 10 | **10** | 0 | 0 | 6,216 | all |
| pcb442 | 442 | 50,778 | 10 | 0 | 1.51% | 0.99% | 601 | all |
| rat783 | 783 | 8,806 | 5 | 0 | 3.72% | 3.39% | 293 | all |

| criterion | required | construction-only | with 2-opt | |
|---|---|---|---|---|
| every tour a valid permutation | all | all 35 | all 35 | **pass** |
| berlin52 at optimum | >= 8/10 | 10/10 | 10/10 | **pass** |
| kroA100 at optimum | >= 5/10 | 0/10 | 10/10 | **pass** |
| kroA100 worst gap | < 2% | 0.45% | 0 | **pass** |
| pcb442 median gap | < 5% | 14.10% | 1.51% | **pass** |

Every criterion the construction-only engine failed now passes, and the two it
already passed it now passes at zero.

This is the predicted result, not a discovered one. The section above named the
missing 2-opt as the cause of the gap *before* these numbers existed, and said
adopting it would close most of it. That ordering is the whole point: the fix
was a standard component of MMAS-for-TSP that the spec had simply never named,
and no frozen parameter was touched to obtain the table.

Three things the table does not say:

- **It is not evidence that 2-opt is cheap.** The `iters/s` column is not
  comparable to the top table, which was measured with `ACO_DIST_CACHE=0`.
  These runs have the cache on, so a roughly 2x gain from caching and whatever
  local search costs are confounded in that one column. Nothing here isolates
  either.
- **rat783 is still short** of published MMAS-with-local-search results at
  3.72%. Closing that wants 3-opt or don't-look bits. Not pursued: beating LKH
  remains out of scope.
- **It does not change R4's design.** R4 still compares corrupted against
  uncorrupted runs of the *same* configuration, and would have been served by
  the 14% baseline too. The gain here is that the rungs run on an engine that
  is competitive rather than merely correct, so a platform-induced deviation
  shows up against a tight baseline instead of a loose one.
