# The OEIS sweep the research pass never ran

The [OEIS assessment](../portfolio/OEIS-ASSESSMENT.md) found an excellent
curator and no reachable record — but explicitly did **not** run the broad
sweep, and said so: the four families it examined all failed for being famous,
and the obscure tail was where a viable target would live.

This is that sweep, done directly against OEIS rather than through literature.

## A wall worth documenting

**OEIS refuses anonymous search results past the first 100.**

```
$ curl '...&start=110'
Sign in to see search results past the first 100.
```

7,442 sequences carry both `hard` and `more`; 3,286 of those are `nonn`
counting sequences. None of that is reachable ten at a time without an account,
which is very likely why the earlier pass verified registry mechanics
thoroughly and never produced a candidate list — an automated fetcher hits this
wall and simply sees nothing.

Worked around by partitioning into 34 narrow topic queries, each comfortably
under the cap, covering the families the brief named: Latin squares,
quasigroups, Steiner systems, designs, magic squares and cubes, Sidon sets,
difference sets, polycubes, polyominoes, lattice animals, tilings, packings,
self-avoiding walks, Costas, Golomb, queens, tournaments, necklaces, matroids,
posets, semigroups, triangulations, polytopes, hypergraphs, matchings,
Hamiltonian counts, dissections, labelled and regular graphs, Ramsey, covering
codes, pattern-avoiding permutations, knots.

**388 unique sequences** harvested and scored.

## How they were scored

Deliberately scored *against* the pattern that killed every earlier candidate.
`triage.py`:

| signal | weight | why |
|---|---|---|
| age of last dated extension | +2/year, capped at 20 | the `ext` field carries "a(12) from _Name_, Mar 14 2004", so staleness is directly measurable |
| GPU / FPGA / cluster / BOINC / distributed.net / CPU-years in the record | **−40** | fame attracts hardware; these frontiers are already owned |
| transfer matrix / DP-over-state / BDD / ZDD | **−25** | needs shared state, fails the stateless-worker requirement outright |
| growth ratio between the last two terms | +10 if 1.5–10×, −12 if >10⁴ | a next term 2–50× the last is plausible; a million-fold jump is not |
| ≤14 terms | +6 | few terms means each one is expensive, which is the signature wanted |

## Top of the ranking

| seq | stale | terms | ratio | what it counts |
|---|---:|---:|---:|---|
| A013579 | 17.2y | 10 | 7.9 | inequivalent Mendelsohn triple systems MTS(n,1) |
| A027567 | 23.5y | 6 | — | n×n pandiagonal (panmagic) squares |
| A000103 | 19.3y | 24 | 6.1 | n-node sphere triangulations, min degree 4 |
| A031436 | 24.8y | 19 | 14.9 | proper linear spaces of order n |
| A001426 | 20.2y | 11 | 304.8 | commutative semigroups of order n |
| A000944 | 20.3y | 18 | 16.8 | polyhedra (3-connected simple planar graphs) |
| A000112 | 20.7y | 17 | 65.7 | posets on n unlabeled elements |
| A006204 | 17.3y | 17 | 12.5 | starters in the cyclic group of order 2n+1 |

The scoring behaved: **A000944 and A000103 surface with their owners attached** —
Brendan McKay and the `plantri`/Surftri toolchain are right there in the
extensions. Those are exactly the "already taken" cases, and the ranking shows
them rather than hiding them.

## Triage of the leaders

**Out on arithmetic.** A000112 posets (last term 4.5×10¹⁵, next ~66× that),
A001426 commutative semigroups (next ~10¹²), A000944 polyhedra (last 1.08×10¹⁴,
and McKay's), A006204 starters (next ~2.7×10¹³). Enumerating objects one at a
time does not reach 10¹²–10¹⁵ at this budget however many workers you have.

**Marginal.** A000103 triangulations — last term 1.67×10¹³, growth only 6.1×,
which puts the next term around 10¹⁴ objects. In C with `plantri` that is
merely large; in pure Python it is roughly 10⁵ core-hours, at the very edge of
budget, against a toolchain whose author is still active. Not where a first
attempt should go.

**The genuinely small ones**, and the honest problem with them:

- **A013579** — MTS counts are 3, 18, 143 at orders 7, 9, 10. Mendelsohn
  triple systems exist only for n ≡ 0,1 (mod 3), n ≠ 6, so the next open order
  is 12. Output counts that small are very plausibly enumerable by isomorph-free
  generation, which parallelises statelessly via canonical augmentation.
- **A031436** — proper linear spaces. Orders 17 and 18 were each their own
  Betten & Betten paper (1999–2000); the next is 19, at roughly 3.6×10⁷ objects.
- **A027567** — pandiagonal magic squares stop at order 6; order 7 is next.

## The one thing that decides all three, and it is not compute

Every one of these terms came from **the research literature**, not from a
brute-force frontier: Colbourn & Rosa's *Triple Systems*, the Betten & Betten
papers, the Handbook of Combinatorial Designs. These sequences stopped because
design theorists moved on, not because anyone hit a wall.

So the decisive question is not "can we compute it" but **"has someone already
computed it and simply not told OEIS?"** If MTS(12) sits in a 2004 design-theory
paper, computing it is a nice exercise and the OEIS contribution is a
transcription — worth doing, but not a record and not a reason to rent a swarm.

That is a literature check on three specific quantities, and it is cheap. It
should happen before any code is written.

## Reproducing

```sh
./sweep.sh                    # 34 topic queries, throttled, under the 100 cap
python3 triage.py oeis_raw.json
```

Full ranking of all 388 in [`ranked.json`](ranked.json).
