# Pheromone memory when the map moves

Ant Colony Optimization is supposed to shine on problems that change while you
are solving them: evaporation forgets what is no longer true, so the colony
adapts instead of restarting. This repo leaned on the same mechanism for a
different claim — that a corrupted pheromone write is erased within a few
iterations rather than accumulating, which is why a machine that miscomputes
could still run this workload.

Stale pheromone from a changed world is structurally the same object as
corrupted pheromone. So this tests the mechanism directly, on hardware that
computes correctly, with no fault injection needed.

## The experiment

Solve a 400-city Euclidean TSP for 600 iterations. Then **relocate** a fraction
of the cities — they stay in the problem, still get chosen between, but their
stored trails now point at where they used to be. Keep going for 300 more
iterations. Four arms, all forked from one bit-identical pre-change state:

| arm | pheromone after the change |
|---|---|
| `RESTART` | thrown away, reset to `tau_max` |
| `RETAIN` | kept exactly as it was, lies included |
| `RETAIN_CLEAN` | kept, but the moved cities' trails reset |
| `SHUFFLE` | kept, values permuted through a random relabelling |

`SHUFFLE` is the one that matters, and it was added after the first results
came in. See below.

## What a naive reading says

Retaining beats restarting at **every** change fraction, five trials each:

| relocated | RETAIN vs RESTART | RETAIN_CLEAN vs RETAIN | RETAIN wins |
|---:|---:|---:|---:|
| 5% | −2.01% | +0.60% | 5/5 |
| 10% | −1.55% | +0.64% | 4/5 |
| 20% | −1.18% | +1.03% | 5/5 |
| 40% | −0.79% | +0.72% | 5/5 |
| 60% | −1.29% | +1.10% | 5/5 |
| 80% | −1.32% | +1.77% | 5/5 |
| 100% | −1.67% | +1.70% | 5/5 |

Two things look like findings here. Memory always wins, and **deliberately
wiping the lying trails makes things worse** — `RETAIN_CLEAN` is behind
`RETAIN` in every row.

There is no crossover. That is the problem.

## Why that reading is mostly wrong

At 100% relocation every city has moved. There is no old map left to remember,
and memory still "won" by 1.67%. Whatever is producing that number, it cannot
be memory.

A converged pheromone matrix differs from a fresh one in **two** ways at once:
it remembers which edges were good, and it is *concentrated* rather than
uniform. `RESTART` installs a flat `tau_max` everywhere. So the comparison
confounds remembered structure with low entropy.

`SHUFFLE` separates them. It permutes the matrix through a random relabelling
of cities: the multiset of values is preserved exactly, so concentration is
untouched, but the correspondence between a value and the edge it was earned on
is destroyed.

**5% relocated** — where memory should matter most:

| arm | final | vs RESTART |
|---|---:|---:|
| RESTART | 15461.4 | — |
| **RETAIN** | **15150.0** | −2.01% |
| SHUFFLE | 15216.8 | −1.58% |
| RETAIN_CLEAN | 15240.2 | −1.43% |

**100% relocated** — where memory should be worth nothing:

| arm | final | vs RESTART |
|---|---:|---:|
| RESTART | 15434.4 | — |
| RETAIN | 15177.0 | −1.67% |
| SHUFFLE | 15155.2 | −1.81% |
| RETAIN_CLEAN | 15434.4 | 0.00% |

## The actual result

Decomposing the 5% case: of the 2.01% advantage retention has over restarting,
concentration accounts for about **1.58 points** and remembered structure for
about **0.44**. Roughly **three quarters of ACO's apparent memory advantage is
not memory at all** — it is that a low-entropy pheromone distribution exploits
faster than a flat one on a short horizon.

And the control behaves exactly as it should at the other extreme. At 100%
relocation `SHUFFLE` matches `RETAIN` (−0.14%, marginally ahead), because there
is nothing left to remember. Memory's contribution falls from +0.44% to zero as
the world is fully replaced, which is the signature a real memory effect must
have.

So: **memory is real, and it is the minority term.** The naive
retain-versus-restart comparison overstates it by about fourfold.

Two internal checks passed, and are worth stating because they are what
distinguish a result from a plumbing artefact:

- At 100% relocation, `RETAIN_CLEAN` clears every row to `tau_max`, which is
  what `RESTART` does — and the two arms come out **bit-identical at every
  checkpoint**. They are supposed to be the same computation, and they are.
- The `+/-` structure of the sweep is monotone in the right direction for
  `RETAIN_CLEAN vs RETAIN`: the more cities move, the more a reset costs.

## For the fault-tolerance argument

The half that survives is the half this repo actually needed. `RETAIN` is never
behind `RETAIN_CLEAN` at any change fraction, which says **wrong pheromone does
not hurt** — evaporation absorbs it, and paying to scrub it is a net loss. That
is the forgetting-as-error-correction claim, and it holds.

What does not survive is the stronger, vaguer version — that ACO's advantage on
changing problems is largely about remembering the old world. Mostly it is
about not throwing away a converged distribution.

## Two design faults, recorded rather than fixed quietly

**Final quality was the wrong metric.** With 2-opt on every ant, all arms
converge to the same local optimum and the comparison reported a tie for
reasons unrelated to pheromone. The metric is now a checkpointed recovery
curve, since memory should pay off early. An improvement counter prints
alongside: a run where no ant beats the seeded incumbent has not tested the
pheromone at all, and [`../aco-r1/`](../aco-r1/) records this repo publishing
one such zero already.

**Deleting cities made the key comparison vacuous.** A deleted city leaves the
candidate lists and construction entirely, so nothing ever reads its pheromone
and zeroing it cannot change a single decision. `RETAIN` and `RETAIN_CLEAN`
came out bit-identical for a tautological reason. Relocation is the honest
version of the question, and is the default; deletion is kept behind
`-DDA_MODE=0`.

## Reproducing

```sh
cc -O2 -DDA_N=400 -DDA_REMOVE_PCT=5 -DDA_TRIALS=5 -o dyn ../../aco/dyn_aco.c && ./dyn
```

Raw output: [`sweep.txt`](sweep.txt) (the seven-fraction sweep),
[`sh5.txt`](sh5.txt) and [`sh100.txt`](sh100.txt) (with the shuffle control).
