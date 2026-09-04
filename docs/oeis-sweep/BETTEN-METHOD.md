# What the Betten papers actually say about cost

Read directly: *The Proper Linear Spaces on 17 Points* (Discrete Applied
Mathematics 95, 1999, preprint dated 30 March 1999) and *Note on the Proper
Linear Spaces on 18 Points* (ALCOMA'99 proceedings). Both are scanned image
PDFs with no text layer — 28 characters extracted across 29 pages — so they had
to be read as images.

## The headline: there is no cost figure to extract

**Neither paper reports a runtime, a machine, or a CPU-hour count.** These are
mathematics papers; computational effort simply is not part of what they
report. The 18-point note is a data paper — tables of counts and example
geometries — and the 17-point paper presents the method and the classification,
not its price.

So the plan of "read the papers for the cost figures and extrapolate to v=19"
does not survive contact with the papers. That is worth stating plainly, since
it was the recommended next step.

## What is there instead, and it is better

The method is **TDO** — tactical decomposition by ordering — and it is a
two-step construction, not a naive search:

1. For a given **line case** (a distribution of block lengths, e.g. 4¹⁸3¹⁵),
   compute every TDO-scheme the requested geometries could have.
2. For each TDO-scheme, construct all geometries realising it.

A TDO-scheme is called **discrete** when every point class and block class is a
singleton. In that case *the scheme is already the incidence matrix* and there
is nothing left to do. Only non-discrete schemes go to the generator.

The v=18 totals make the shape of the work unmistakable:

| quantity | v = 18 |
|---:|---:|
| TDO-schemes | 2,258,639 |
| of those, non-discrete | **2,367** (0.105%) |
| geometries from non-discrete schemes | 156,618 |
| geometries from discrete schemes | 2,256,272 |
| **total proper linear spaces** | **2,412,890** |

**The expensive generator runs on one case in a thousand.** Ninety-three percent
of the answer falls straight out of the cheap parameter-refinement step.

## Why that matters for this platform

The decomposition is close to ideal for stateless workers:

- **The natural partition is the line case**, and line cases are independent of
  one another. In the v=18 table they are numbered 6 through 280 with gaps —
  roughly seventy that actually produce schemes.
- **Results combine by summation.** Column 6 of their Table 1 is literally
  `column 3 − column 4 + column 5` per line case, totalled at the bottom. That
  is a worker returning three integers.
- **No shared state is needed between line cases** — which is exactly what
  disqualified the polyomino transfer-matrix approach.
- The cost is dominated by cheap refinement with a small expensive tail, so
  work units are naturally chunky enough to clear the granularity floor.

Isomorph rejection, the usual reason enumeration resists parallelism, is
handled inside a line case by the TDO invariant plus the generator — not by a
global table of everything seen so far.

## So how do you size v = 19?

Not from the literature — it is not there. The honest answer is to **measure it
ourselves, by reproducing a known value.**

Reimplement TDO and recompute **v = 17 (161,925)** and **v = 18 (2,412,890)**.
Both are published, so they are exact calibration targets: get the number right
and the implementation is validated; get it wrong and stop. Then measure what
those cost and extrapolate to 19.

That is the same discipline used everywhere else here — reproduce a known
answer before chasing an unknown one — and it converts an unsized problem into
a measured one for a few days of work rather than a compute budget.

Two cautions on that path. Reimplementing TDO from a 1999 paper is a real
research-software task, not a weekend port; the method is presented in full
detail, which is why the 17-point paper runs to 29 pages. And the v=17→18 jump
in *output* was 14.9×, but nothing establishes that the *work* scales the same
way — that is precisely what the calibration would tell us.

## One loose end

Reference [6] of the 18-point note is an **addendum** hosted at
`mathe2.uni-bayreuth.de/betten/PUB/pub.proper18.html`. A 2000-era university URL
for an author who has since moved twice; likely dead, but if it survives in an
archive it may carry the computational detail the papers omit.
