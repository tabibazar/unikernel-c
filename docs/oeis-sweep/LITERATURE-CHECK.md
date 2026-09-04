# Literature check on the three candidates

Research pass, 2026-09-04. 101 agents, 938 tool calls. The question was
deliberately narrow: **has the next value already been computed and published,
and OEIS simply never told?**

One disqualified, one still alive, one unassessed. The disqualification is the
most valuable result here — it saved a campaign.

## 1. Mendelsohn triple systems — DISQUALIFIED, and it was close

**A013579 stops at n=10 = 143. The next two values are already published.**

> Khatirinejad, Östergård & Popa, *The Mendelsohn Triple Systems of Order 13*,
> Journal of Combinatorial Designs 22(1):1–11, online 2013-07-31,
> DOI [10.1002/jcd.21364](https://doi.org/10.1002/jcd.21364)
>
> Publisher abstract, verbatim: *"By means of a computer search, we classify all
> Mendelsohn triple systems of order 13 with λ=1; there are 6 855 400 653
> equivalence classes of such systems."*

"Equivalence classes" is the same quantity A013579 calls "inequivalent",
confirmed by an arithmetic consistency check against independently reported
isomorphism-class counts. A 2026 paper (Kozlik) attributes the MTS(12)
isomorphism-class count — **9,801,188** — to that same 2014 paper, so a(12) is
almost certainly in there too.

**Note how close this was.** 9.8 million objects at order 12 is comfortably
enumerable at our budget; had this not been published we would have had a real
target. It was published eleven years ago and OEIS was never told — the entry
was last edited July 2025 and still cites only Colbourn–Rosa and the Handbook.

### The consolation prize is real, and it costs nothing

**A013579 can be extended by transcription.** That is a genuine, credited OEIS
contribution — name and date in the Extensions field — available this week for
zero compute.

Two cautions before anyone submits. The MTS(12) figure reaches us **second-hand**
via the 2026 paper, not read from the 2014 one; and OEIS terms must match the
entry's own definition exactly. Read the Khatirinejad paper's definition of
"equivalent" before proposing anything — *inequivalent*, *nonisomorphic* and
*distinct* are three different counts for triple systems, and submitting the
wrong one is worse than submitting nothing.

## 2. Proper linear spaces on 19 points — GENUINELY OPEN

**A031436 stops at n=18 = 2,412,890** (Betten & Betten, ALCOMA'99 proceedings,
Springer 2000/2001). The entry is still flagged `hard,more`, last edited January
2019.

Anton Betten's own publication list and his **complete dblp record — 31 entries,
1995 to 2026 — contain no linear-space enumeration past 18 points.** Nobody
else appears to have continued the programme either.

So this one survives the check. But it survives with a hole in it:

> **No source anywhere gives an estimate, a bound, or a partial count for
> v = 19.**

That is a genuine planning problem, not a detail. The output count grew 14.9×
from 17 to 18 points, which would suggest ~3.6×10⁷ spaces at 19 — but the
*search* is vastly larger than the output in isomorph-free generation, and
without a published figure we would be committing compute against a number we
invented. The two previous terms were each a full research paper, which is
itself a signal about the difficulty.

## 3. Pandiagonal magic squares of order 7 — UNASSESSED

**Zero claims about A027567, Walter Trump, or any pandiagonal enumeration
survived verification.** The pass did not investigate it. This is absence of
evidence, not evidence of absence, and it needs its own search rather than an
assumption either way.

## Where that leaves it

| candidate | status |
|---|---|
| Mendelsohn triple systems | **dead as a compute target** — published 2014. Live as a free transcription. |
| Proper linear spaces, 19 points | **open**, no competitor, no size estimate to plan against |
| Pandiagonal order 7 | **unknown** — never checked |

The honest reading: the sweep and this check together did what they were for.
They found one target that was already taken — cheaply, before any code — and
one that is genuinely unclaimed but unsized. Neither outcome is a target we can
responsibly start computing tomorrow.

Two cheap next steps, in order:

1. **Transcribe MTS(12) and MTS(13) into A013579**, after reading the source
   paper's definition. Zero compute, a real credited contribution, and it puts a
   name on the board while the bigger question resolves.
2. **Size the linear-spaces problem.** Read the Betten & Betten papers for what
   17 and 18 points actually cost them — runtime, machine, search-tree size.
   That is the number that decides whether 19 is a weekend or a decade, and it
   exists in the literature even though the v=19 count does not.
