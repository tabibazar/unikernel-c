# Percolation thresholds: assessed, out of reach

Research pass, 2026-09-03. 107 agents, 24 sources, 119 claims extracted, 25
verified, 18 confirmed.

**Verdict: probability of moving a published percolation threshold within
100,000 core-hours is effectively zero.** Every candidate fails at least one of
the three constraints, and all of them fail cheap external verification.

## The structural problem: the records are not Monte Carlo

The best 2D thresholds are produced by **deterministic algebra**, not sampling.
Scullard and Jacobsen equate the largest eigenvalues of two transfer-matrix
sectors in the periodic Temperley-Lieb algebra:

| threshold | value | source |
|---|---|---|
| kagome bond | 0.52440499916744820(1) — ~17 sig figs | Scullard & Jacobsen, PRResearch 2, 012050 (2020) |
| square site | 0.59274605079016(1) | Jacobsen, J. Phys. A 57, 258002 (2024) |
| Archimedean bond | ~1e-13 | Jacobsen, J. Phys. A 47, 135001 (2014) |

Monte Carlo sits **four to nine digits behind** this, and the gap is not a
compute gap. The method's scaling axis is basis size in one coupled
eigenproblem, not sample count — so it fails the combination-operator criterion
outright, and no amount of independent stateless workers approaches it.

## The Monte Carlo frontiers that do suit the substrate

Three candidates pass the combination-operator test cleanly. Each dies
elsewhere.

**Invasion percolation in d>=4** — Mertens & Moore, PRE 98, 022120 (2018), 5D
bond p_c = 0.11817145(3). The combiner passes: 10^6 fully independent runs
averaged at 68 checkpoints. Compute to replicate is even *in budget* at roughly
6,000-60,000 core-hours. **Killed by memory:** each run holds a cluster of up to
11,021,797 vertices plus a boundary priority queue of order 3e8 entries in one
address space — GB scale. At ~16 B/entry, 16 MiB caps cluster mass near 4e4,
about **1,000x short**, and it is precisely the large-N points that carry the
extrapolation.

**4D bond thresholds by Leath growth** — Xun & Ziff, PRResearch 2, 013067
(2020), 10^9 independent samples binned into 15-17 histogram bins. This is the
cleanest criterion-(b) pass in the entire evidence set: the merge is an
elementwise integer sum of tiny histograms. **Killed by memory:** the published
configuration uses a lattice array of L^4 = 2^28 sites, ~268 MB even at one byte
per site, 16-64x over budget.

**2D continuum percolation** — Mertens & Moore, PRE 86, 061109 (2012),
eta_c = 1.12808737(6). Combiner passes; the small-L arms even fit 16 MiB.
**Killed by cost:** the existing precision consumed roughly 400 laptop-years,
about **3.5 million core-hours — 35x the entire budget** — and one further digit
costs 100x that. A generous 3-5x modern-core speedup still leaves it ~700x over.

## The candidate that fits the hardware perfectly, and is pointless

Newman-Ziff union-find on 2D lattices is a near-ideal match: two arrays of size
N (a 128x128 lattice is 131 KB), a binomial average over independent 0/1
wrapping outcomes, workers returning a count and the merge being integer
addition. The published scaling even *favours small lattices* at fixed compute
— Newman and Ziff call larger lattices "not only unnecessary, but also
essentially worthless".

It fails for a reason no engineering fixes: the 2D lattices it applies to are
exactly the ones whose records the Temperley-Lieb methods now hold, four to five
digits ahead. There is no digit left to add.

## Two general lessons worth keeping

**One more digit costs 100x.** Monte Carlo error falls as 1/sqrt(N), so any
target whose figure of merit is significant digits of a real number is a
100x-per-digit treadmill. That is a poor match for a fixed budget, and it
generalises well beyond percolation.

**Monte Carlo estimates fail cheap verification by construction.** A threshold
can only be replicated statistically, never re-derived exactly the way an
enumeration integer or a prime certificate can. Every percolation candidate
fails criterion (a) regardless of its other properties — which retroactively
sharpens why the chain hunt is the right target: a chain is an integer fact, not
an error bar.

## The one open avenue, not a recommendation

*(low confidence)* Lattices with no Temperley-Lieb construction — 3D lattices
and complex-neighbourhood variants — are still done by Newman-Ziff Monte Carlo
(Malarz, arXiv 2023/2025; Scullard et al., arXiv 2021-02-25). Those thresholds
sit far below TL precision, are embarrassingly parallel with an additive
combiner, and their small-L arms fit 16 MiB. The discriminating fact — the
sample count behind their current error bars — was not established, so whether a
digit is reachable is unknown.

## Scope warning

This pass produced **zero verified claims** on self-avoiding walks, on
OEIS/lattice-animal/combinatorial targets, and on Monte Carlo reproducibility
norms. Those sections are **unresolved, not declined** — the third pass running
where the brief's later sections were not reached. SAW in particular remains
genuinely unevaluated after two attempts.
