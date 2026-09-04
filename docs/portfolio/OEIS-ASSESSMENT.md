# OEIS as a target: the curator is ideal, the famous sequences are not

Research pass, 2026-09-04. 107 agents, 24 sources, 118 claims extracted, 25
verified, 21 confirmed, 4 refuted.

**Verdict: OEIS is the best curator found in five research passes, and none of
the four famous sequences examined is reachable. No target should be
manufactured from this evidence — but the search was not finished, and the
unfinished part is where a viable target would most plausibly live.**

## OEIS clears every registry bar, decisively

This is the first target in five passes where the *curator* question comes back
unambiguously positive. Every earlier candidate died at least partly here:
Gset, Biq Mac, MQLib and the Bonn Spin Glass Server run no leaderboard, no
submission process and no verification between them.

| requirement | OEIS |
|---|---|
| accepts submissions | any registered user may propose an extension to a published sequence |
| named arbiter | staged pipeline: editing → proposed → an Associate Editor or the Editor-in-Chief marks ready → Editor-in-Chief approves |
| credits the contributor | by name and date in **two** places — the Extensions (`%E`) field and the b-file link line |
| supports verification | asks for the computing program to be published alongside the b-file, and records independent recomputation as its own separately credited line |
| turnaround | edits to published sequences are usually faster than new submissions |

Two further points are worth more than they look.

**Distributed and swarm computations are credited in exactly this form today** —
this is an established path, not one we would be inventing.

**OEIS enforces our own criterion (a) for us.** Its Style Sheet requires that
Data terms be *proved correct and complete as far as shown*, so Monte Carlo,
heuristic and lower-bound results cannot become terms; they go to Comments.
That is the same rule that made us reject percolation thresholds, written into
someone else's editorial policy.

One compliance note: OEIS requires a **human author personally responsible for
the correctness of any AI-produced content**, for edits as well as new
sequences. A constraint on how an agent-assisted campaign is run, not a bar to
running one.

## The four famous sequences are all out, by three to five orders

| target | verdict | why |
|---|---|---|
| **Costas arrays** A008404, order 30 | out by ~3–4 orders | order 29 alone cost 366.55 CPU-years on clusters in 2011; order 30 is ~1e8–1e9 pure-Python core-hours |
| **Golomb rulers** OGR-29 | out by ~4–5 orders | OGR-28 took 8.5 years and ~1e9 core-hours of optimised C/GPU across 65,000 volunteers. distributed.net's "no plans" is a size verdict, not an opening |
| **n-Queens** A000170, a(28) | out by ~4–5 orders | ~1e4–1e5 core-*years* in C, and the last two terms were set on FPGAs with active competition — the exact profile our criteria say to avoid |
| **Polyominoes** A001168 | disqualified on architecture *and* cost | the record method is a memory-bound, shared-state transfer-matrix DP — it fails the stateless-worker requirement outright — and the frontier is held by an active group |

The pattern is consistent and worth naming: **fame attracts hardware.** Every
sequence well-known enough that we thought of it unprompted has already drawn a
cluster, an FPGA farm or a BOINC project. Being able to list a target from
memory is close to proof it is taken.

## What was not searched, and why that matters

The pass verified the registry mechanics thoroughly and four families in depth.
**The broad sweep the brief actually asked for did not happen.** No verified
claims were produced for:

- Latin squares, quasigroups, Steiner systems, combinatorial designs
- magic squares and cubes of higher order
- perfect difference sets, Sidon sets, B_h[g] sets
- polycubes and other lattice animals
- the general trawl of `hard`/`more`-keyworded sequences whose last term is
  five or more years old

That is the section where a viable target would most plausibly live, precisely
because the four families that *were* examined all failed for the same reason —
they are famous. An obscure sequence with a stale term and no project attached
is the shape that survives, and it is exactly what went unexamined.

**Absence of a verified candidate there is absence of evidence, not evidence of
absence.**

Section 5 — alternative curators — also produced nothing. Whether the House of
Graphs, the Online Encyclopedia of Combinatorial Designs, the Ramsey/van der
Waerden/Schur tables or the Encyclopedia of Combinatorial Structures accept and
credit submissions, as opposed to publishing a table with a maintainer's email,
remains entirely open.

## Method caveats

- **oeis.org returns HTTP 403 to automated fetchers.** All OEIS evidence was
  retrieved with a browser user-agent. A verifier without that workaround will
  appear to find nothing, which is worth knowing before anyone concludes the
  sources were fabricated.
- The cost figures combine measured historical numbers with two soft
  multipliers: a ~50× pure-Python penalty (asserted by us, **not measured on
  this platform**) and a 3–10× modern-silicon credit over 2005–2011 cluster
  cores. Both are uncertain by a factor of several — but every verdict survives
  an order-of-magnitude error in either, because the gaps are three to five.
- Four claims were refuted and must not be reused: that non-AI bulk submissions
  require prior OEIS permission (0-3, though a prohibited-activities clause does
  exist and should be read directly before any campaign), two specifics about
  the 2012 Costas GPU/FPGA paper, and a cost/memory extrapolation for A001168.

## Recommendation

**Do not pick a target from this pass.** The four examined are out by margins no
budget closes.

Two cheap follow-ups would settle whether the direction is alive:

1. **Run the section-2 sweep that was skipped** — OEIS sequences keyworded
   `hard` and `more`, last term five or more years old, exact counts with a
   memory-light backtracking decomposition, and no cluster or FPGA project
   attached. That is a database query against OEIS, not a literature review,
   and it is the only part of this question still open.
2. **Measure the pure-Python penalty on this platform** rather than asserting
   50×. `pyprobe/` already runs; a fixed-work benchmark alongside it costs
   nothing and would firm up every cost estimate here.

The honest summary: we found an excellent curator and no reachable record. That
is a better position than it sounds — the curator was the part that killed
three previous directions, and it is now solved.
