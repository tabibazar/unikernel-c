# Cunningham chain size records: what a small budget actually buys

Research pass, 2026-09-03. 106 agents, 23 primary sources, 97 claims extracted,
25 verified, 19 confirmed. Findings carry the confidence label verification gave
them; three of eight rest on derivation rather than sources and are marked.

**Headline: the money is not the constraint.** Even the out-of-budget targets are
a low four-figure bill. What gates this is wall-clock time, one specific
unmeasured software ratio, and a live incumbent who resets bars in weeks.

## The target is real, tabulated, and refereed

The "abandoned backwater with no verification infrastructure" hypothesis is
**false** and should be dropped. Three mutually consistent registries maintain
per-length size records: [pzktupel.de/CC/CC.php](https://www.pzktupel.de/CC/CC.php)
(Luhn, continuing Dirk Augustin, updated 25 Aug 2026), t5k.org Top-20 pages id=19
and id=20, and [primerecords.dk](http://primerecords.dk/Cunningham_Chain_records.htm)
(Andersen/Augustin, 7 Mar 2026). t5k accepts submissions for both kinds.

Better still: for the record forms `m*p#*2^j +/- 1`, `N-1` or `N+1` is **fully
factored**, so PFGW yields deterministic Pocklington/Morrison proofs at
negligible extra cost. These are proven primes, not probable primes. A find is a
certificate, not a claim.

### Current 1st-kind size records (first-term digit convention)

| k | digits | date | holder |
|---:|---:|---|---|
| 2 | 388,342 | 29 Feb 2016 | PrimeGrid/Brown |
| 3 | 20,013 | 17 Feb 2020 | Paridon |
| 4 | 4,362 | 16 Feb 2025 | Batalov |
| 5 | 2,065 | 1 Aug 2026 | Batalov |
| **6** | **1,016** | **25 Mar 2015** | **Batalov** |
| 7 | 518 | 21 Apr 2026 | Batalov |
| 8 | 342 | 14 Aug 2026 | Batalov |
| 9 | 260 | 10 Mar 2016 | Balyakin |
| 10 | 156 | 17 Aug 2026 | Batalov |

2nd kind: k=2 169,015 (Mar 2023); k=3 14,784 (2 Jan 2025); k=4 4,809
(17 Jul 2026); k=5 2,069 (4 Aug 2026); k=6 1,001 (28 Jan 2025).

Digit counts differ by 1 between sources (1,016 vs 1,017) because t5k reports the
**final** term and the others the **first**. Not a disagreement. The form also
changes at k=4: k<=3 records are base-2 Proth/Riesel forms, k>=4 are primorial.

## Who you would be racing

**Not a project -- a person.** PrimeGrid's active subproject list (fetched
2026-09-02) contains **no Cunningham chain search**: 321, Cullen, Woodall, Proth,
Seventeen or Bust, SR Base 5, Riesel, GFN, AP27 and others, but nothing for
chains of length >= 3. There is no BOINC-scale competitor to outrun.

Instead, **of the 14 records at k=4..10 across both kinds, Serge Batalov holds
13** -- sole exception k=9 first kind (Balyakin, 2016). He reset k=4, k=5 both
kinds, k=7, k=8, k=9 second kind, k=10, k=15 and k=16 during 2026 alone, several
within days of the registry's last update. Every other prover code on t5k has
been silent since 2023.

That cuts both ways: nothing organised to outspend, but a live incumbent who
demonstrably resets a bar in the time it takes to run a hunt.

## The discriminator: stale-and-unattended vs stale-and-hard

The table supplies its own test. First- and second-kind chains have near-identical
admissible-pattern densities, so **a mirror-kind record reached at the same digit
size with publicly available tools proves no hardness wall exists there.**

**Class A -- unattended.** k=6 first kind, 1,016 digits, untouched since
25 Mar 2015, while the mirror second-kind record at essentially the same size
(1,001 digits) was taken **28 Jan 2025 with ordinary NewPGen/PFGW**. Same pattern
at k=9 first kind (260 digits, 2016) against second kind reaching 341 digits in
Aug 2026.

**Class B -- genuinely hard.** k=11-14, all set by the **Primecoin
proof-of-work network** 2013-2018 (k=11 140 digits Aug 2013; k=12 113 digits
May 2014; k=13 107 digits Jan 2014; k=14 98 digits Nov 2018). Those bars were set
by network-scale hashrate, not an individual. The tell: k=10 first kind is now
**156 digits**, *larger* than the k=11 record of 140 -- an unfilled gap bracketed
above and below by 2026 activity, explained by cost, not neglect.

## The cost model, validated against standing records

*(medium confidence -- derived this session, not sourced)*

For a primorial form `m*p#*2^j +/- 1`, every term is coprime to all primes <= p,
so **the primorial is the sieve** and the multiplier m is already a survivor to
depth p. With Caldwell's local density `w(p) = min(k, ord_p(2))`:

    P(full chain) = (e^gamma * ln p / ln N)^k

Validated against three standing records -- predicted vs observed multiplier:
k=4 ratio 0.82, k=5 ratio 0.32, k=6 ratio 1.72. **Three independent hits within
3x.** Caldwell's own Table 7 overshoots actual counts by ~1.5x at k=6, so carry
+/- one order of magnitude.

Critically, `B_k` must **not** be applied on top of post-sieve survivors -- it is
already consumed by the sieve. Sorenson & Webster (Math. Comp., DOI
10.1090/mcom/3501) confirm there is no algorithmic escape: the best known
pattern-sieve is O(n/(log log n)^k), a poly-log-log improvement, not an
exponential one.

## Ranked shortlist

*(medium confidence; native Linux with GMP/PFGW; sieve depth ~1e13)*

| # | target | digits | PRP tests | core-hours | record to beat | verdict |
|---|---|---:|---:|---:|---|---|
| **1** | **k=6, 1st kind** | ~1,050-1,100 | ~9e9 | **6,000-15,000** | 1,016 d, **25 Mar 2015** | **IN BUDGET** |
| 2 | k=7, either kind | ~600 | ~8e9 | 1,500-3,000 | 518 d, 21 Apr 2026 | in budget, freshly contested |
| 3 | k=5 | ~2,100 | ~6e9 | 25,000-60,000 | 2,065 / 2,069 d, **days old** | marginal |
| 4 | k=4 | ~4,900 | ~2e9 | 85,000+ | 4,809 d, 17 Jul 2026 | out |

Also out: k=9-10 (sieve-bound, ~1e17 marks) and the k=11-14 Primecoin band
(~1e5-1e6 core-hours -- stale *and* genuinely hard).

**k=6 first kind is the pick.** Not because it is cheapest -- k=7 is 4-10x
cheaper -- but because it is the only in-budget target that is simultaneously
stale for 11.5 years *and* proven not to be a hardness wall by its own mirror. On
k=7 you would be racing an incumbent who took it four months ago and can retake
it.

The sieve/PRP balance flips at k~9: sieve cost is nearly independent of depth
while deeper sieving cuts the PRP count geometrically, so the equilibrium sits
near depth 1e13 for these sizes, and beyond k=9 the 64-bit sieve becomes the
bottleneck rather than the PRP.

## Substrate verdict: run it on Linux

*(low confidence -- the required benchmark evidence did not survive verification)*

**Not because of gwnum.** gwnum's FFT advantage is concentrated above ~10,000
digits, i.e. k<=4 targets. At 500-1,100 digits GMP `mpz_powm` is near parity, so
the in-budget targets **do not need gwnum at all.**

The blocker is bignum quality in a freestanding port. mini-gmp is a single pure-C
file needing only `malloc`/`memcpy` and would build on the substrate -- but it is
**schoolbook-only**, roughly 3-10x slower at 3,300 bits. Full GMP with
`--disable-assembly` plus a small libc shim keeps Toom/Karatsuba and is the
realistic port. Stack the measured 3.7x substrate penalty
([`../nanos-vs-baremetal/`](../nanos-vs-baremetal/)) and the swarm lands
**5-15x off native**, turning the 6-15k-hour k=6 job into 50,000-200,000 hours.

Only k=7 has enough headroom to survive on the swarm.

Two structural points are load-bearing and cheap to test:

- **No libm is not a blocker.** Modular exponentiation is pure integer
  arithmetic.
- **The sieve stage is exactly 64-bit modular arithmetic** -- the operation this
  repo already measured at 3.7x slower than native. Any sieve-bound target (k>=9)
  eats that penalty undiluted.

## The one measurement that decides this

Before spending anything, time `mpz_powm` on a 3,320-bit modulus (1,000 digits)
three ways: **native GMP, mini-gmp, and under the unikernel.** That three-way
ratio decides whether the swarm is a 5x or a 50x handicap, and that single number
moves k=6 from in-budget to out. It is an afternoon's work and it gates
everything else.

## What this pass did not establish

- **2026 cloud pricing.** No verified figure, for the second research pass
  running. The working prior -- spot $0.01-0.02/vCPU-hr, on-demand $0.04-0.05,
  cheap metal $0.003-0.006 -- puts 50,000 core-hours at roughly $500-1,000 spot
  or $150-300 on metal. Re-price before committing. A cloud vCPU is usually an
  SMT sibling, so effective core-hours can run 30-50% below what is billed.
- **The gwnum/GMP/naive benchmark ratios.** The whole substrate verdict rests on
  model prior, not measurement. Hence the recommendation to measure it.
- **Independent contradiction sweep.** The session's WebSearch budget was
  exhausted (200/200), so every finding rests on direct primary-source fetches.
  For record tables that is the strongest available evidence; for the "nobody
  else is hunting" inference it leaves no cross-check.

## Time sensitivity is severe

The k=4, k=5, k=7, k=8 and k=10 records all moved within the last five months,
several within days of the registry's last update (25 Aug 2026). **Re-check
pzktupel.de immediately before starting** any target other than k=6 first kind or
the k=11-14 band. The incumbent is demonstrably capable of resetting a bar in the
time it takes to run the hunt.

Two upstream transcription errors corrected here: the k=5 second-kind multiplier
is 15863979409328 (not 16863...), and the k=4 first-kind date is 16 Feb 2025 (not
2026).

## Why this target suits a swarm you cannot fully trust

A chain is an **existence claim with a cheap certificate** -- verified in
milliseconds by anyone, and for these forms provable outright. This repo's known
platform non-determinism means the search silently misses some candidates, so no
exhaustive-coverage claim is available. For a size record that costs efficiency,
not validity: you only need to find one, and the finding proves itself.

That is the same property that kept GIMPS in scope when Collatz, BB(6) and QAP
optimality were rejected, and it is the strongest argument for this target over
anything requiring a trusted bound.

## Practical note: no off-the-shelf sieve exists

The de facto toolchain is NewPGen (sieve) + OpenPFGW or LLR (PRP/primality),
both gwnum front-ends, and the record tables carry it in an explicit tools
column on entries as recent as 25 Aug 2026. But mtsieve (Rodenkirch, actively
maintained, last release 2026-07-23) ships `sgsieve`, which covers only k=2 first
kind. **A sieve for chains of length k>=3 is custom work** -- against a
maintained framework whose author documents how to add one, so the honest framing
is "custom work", not "impossible".
