# Spin glasses and Max-Cut: assessed, and not recommended

Research pass, 2026-09-03. 102 agents, 20 sources, 98 claims extracted, 25
verified, 21 confirmed. **Four of the brief's six sections produced zero
surviving claims**, so this assessment is partial and says so throughout.

**Verdict: do not pivot to this on current evidence.** The idea was sound --
same Hamiltonian as the existing Ising work, ground states verifiable in one
O(|E|) energy evaluation, ensemble algorithms that ought to suit fan-out. Three
findings undercut it, and one finding is genuinely encouraging.

## What kills it

**There is no curator.** Gset (Stanford, files frozen at 2003), Biq Mac (frozen
September 2007), MQLib (a code library, reference study 2018) and the
Bonn/Cologne Spin Glass Server (submit an instance, results by email) --
**none** operates a leaderboard, a submission process, or a record-verification
service. G-set best-known values exist only scattered through the heuristics
literature.

This is the decisive difference from the Cunningham target. There, a find is
submitted to t5k and the Luhn/Augustin registry, both of which accept and
publish; the record becomes a fact anyone can point at. Here you would be
self-publishing a number into a literature with no arbiter. Worse, **publishing
the spin configuration is not the community norm and no host requires it** --
so the cheap-verification property that made this attractive is available in
principle and largely unexercised in practice.

**Binary Max-Cut on G-set is picked over.** Exactly one new best-known in a
decade: G63 at 27,047 against a 27,045 record standing since ~2015 (Khan &
Shukla, arXiv:2510.21105), plus Zick's May 2025 improvements to G72/G77/G81
(arXiv:2505.18508). A surface that yields one record per decade is not a
surface a first attempt should aim at.

**The recent record-setter was GPU-resident.** Every recent competitive result
that verified came from GPU hardware -- a single RTX A6000 for the G-set runs,
an eight-GPU A100 node for a 100,000-spin instance. This is the Collatz trap
again: right algorithm, wrong silicon.

## What is genuinely encouraging, and worth remembering

**The MIPLIB failure does not repeat, structurally.** That target died because
every record-setting engine was one long serial trajectory. Here the picture is
mixed -- the standing G-set incumbents (MOH, PLS, BLS, tabu) are indeed CPU
single-trajectory local search, but the one recent record came from Population
Annealing Monte Carlo, an ensemble method.

More importantly, there is a structural point the sources make almost in
passing: **for ground-state energy specifically, combining independent runs is
just `min()`, and needs zero communication.** Thermodynamic observables require
proper ensemble weighting and hence coupling; finding a minimum does not. So
independent stateless restarts are *valid* here in a way they are not for
measuring a free energy. The open question is whether they are *competitive* --
and no verified result settles that either way.

That is worth carrying forward. It is the cleanest statement yet of which
workloads this substrate is actually shaped for: **the ones whose parallel
combination operator is `min` or `max` rather than a weighted average.** The
prime-chain hunt is exactly that shape too.

## What is worth taking, even though the target is not

**Max-3-Cut on the same G-set graphs is the one permeable surface found.** A
single 2026 paper established new best-knowns on **36 instances** spanning
G16-G72, with improvements up to 65 on G64 -- against exactly one new binary
Max-Cut record in the same period. That gap is evidence of an under-attacked
variant rather than a hard one. If this direction is ever revisited, it is the
place to start.

**Biq Mac is a free correctness harness.** Worthless as a record target -- n=20
to 500, mostly proven optima, frozen since 2007 -- but its rudy-generated
toroidal-grid, `ising2.5`/`ising3.0` and `pm1s`/`pm1d` families are exactly the
+/-J Ising model this substrate would run, and they come **with proven optima**.
That is a known-answer calibration set, which is the step this repo takes before
every claim anyway.

## What this pass failed to establish

Stated plainly because the gaps are large enough to change the ranking:

- **The CPU cost model.** No verified spin-flip rate per core, and no figure for
  how many sweeps published results needed to reach named best-knowns. So no
  core-hour estimate here is grounded, and whether 100,000 core-hours is within
  an order of magnitude of a record attempt is **unknown**.
- **The wider competitive landscape.** Nothing verified on Toshiba's Simulated
  Bifurcation Machine, Fujitsu's Digital Annealer, D-Wave, CMOS or coherent
  Ising machines.
- **The alternative physics targets.** Percolation thresholds and self-avoiding
  walks were in the brief and produced nothing. Either could be a better target
  than this one; the comparison that would rank them was not made. For SAW in
  particular the decisive question is whether the frontier is compute- or
  **memory**-limited, since transfer-matrix methods would disqualify a 16 MiB
  worker outright.
- **The reproducibility angle.** Whether bit-identical Monte Carlo across
  substrates is a real contribution or a solved problem nobody finds
  interesting: unanswered.

Two claims were refuted 0-3 and must not be resurrected: that the Spin Glass
Server returns provably exact optima, and that its geometries are restricted to
planar lattices and complete graphs. If Bonn's branch-and-cut *does* certify
optima on 2D/3D +/-J lattices at sizes reachable in 16 MiB, this direction is
dead on arrival regardless of everything above -- and that was not established
either way.

## Recommendation

Stay on the chain hunt. It has a curator, a certificate, a stale eleven-year
record, and no organised competitor -- four properties this target lacks. Revisit
Max-3-Cut only if the chain work completes and the appetite remains, and price
percolation and self-avoiding walks properly before choosing between them, since
this pass never reached them.
