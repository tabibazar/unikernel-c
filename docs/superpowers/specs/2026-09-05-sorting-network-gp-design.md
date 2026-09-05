# Evolving Minimal Sorting Networks — Study Design

**Status: designed, not started.** Nothing here has been run. Execution is on
hold until the Cunningham hunt reports, because both compete for the same four
BareMetal Cloud instances.

## Purpose

Find a sorting network on 18 inputs using fewer than 77 comparators, or
establish honestly that this method does not get there.

Optimal sizes are known exactly for n ≤ 17. From n = 18 upward neither optimal
size nor optimal depth is known, and the best published upper bound for n = 18
is **77 comparators**. Nobody knows whether 76 exists.

## Established before design

These are the facts the design rests on. They were established before the
design, not chosen to justify it.

- **Optimal sizes, n = 9 to 17:** 25, 29, 35, 39, 45, 51, 56, 60, 71. The last
  open cases below 13 (n = 11 and n = 12) were only resolved in 2020.
- **Evolutionary search holds records on this problem.** The smallest known
  network for n = 13 (45 comparators) was found by Hugues Juillé in 1995 by
  simulating an evolutionary process, and still stands. Minimum-depth networks
  for n = 9 and n = 11 were found by Loren Schwiebert in 2001 using genetic
  methods. This is the reason the method is not merely plausible here.
- **Thirty years without improvement at n = 13** is the other half of that fact,
  and it is the prior on how hard this is.
- **Zero-one principle.** A comparator network sorts all inputs iff it sorts all
  2^n binary inputs. Correctness is therefore exactly decidable, not sampled.
- **Measured on this project's own hardware:** the ACO study failed its gate
  without local search (0/10 at optimum on kroA100) and passed with 2-opt
  (10/10). The metaheuristic was not doing the work. That is the failure mode
  this problem must avoid, and the reason it does is stated under *Why this
  problem and not another* below.
- **Granularity floor:** measured at 500–1000 iterations per worker on this
  platform; below it, fan-out stops paying.

## Why this problem and not another

Sorting networks have **no cheap local move**. There is no 2-opt analogue that
nudges a network toward correctness, because correctness is a property of all
2^n inputs at once. Whatever the search does here cannot be a wrapper around a
local search that would have won on its own — which is precisely how the ACO
study went wrong.

The workload is also the shape this platform is unambiguously good at: pure
integer computation, no shared state, no allocation, no floating point, and a
working set of well under a megabyte.

## Representation

A genome is a fixed-length array of comparators, each a pair of wire indices:

```c
typedef struct { uint8_t a, b; } Cmp;    /* a < b, always */
Cmp net[MAX_CMP];                        /* 77 for the n=18 target */
```

Fixed length, not variable. Length is the thing being minimised, so it is a
parameter of the run rather than of the individual: a run targets exactly L
comparators and either finds a correct network of that length or does not.
Searching for "short and correct" simultaneously invites the classic bloat
pathology and a fitness function that has to trade two incomparable things.

Canonical form: `a < b` always, since a comparator that puts the smaller value
on the higher-numbered wire is the same comparator relabelled.

## Evaluation

Each wire holds a 2^n-bit vector, so every possible binary input flows through
the network simultaneously. A comparator is two instructions:

```c
lo = w[c.a] & w[c.b];      /* min, for every input at once */
hi = w[c.a] | w[c.b];      /* max, for every input at once */
w[c.a] = lo;  w[c.b] = hi;
```

**Initialisation.** Wire i is loaded with the bit pattern in which bit k is set
iff the i-th bit of k is set — that is, the 2^n inputs enumerated in binary,
one per bit position. Standard alternating-block masks, generated once per run.

**Correctness.** For 0-1 vectors, an output is sorted iff no wire holds a 1
directly above a wire holding a 0:

```c
uint64_t bad = 0;
for (int i = 0; i + 1 < n; i++)
    bad |= w[i] & ~w[i+1];       /* per word, accumulated over the vector */
```

A network is correct iff `bad` is zero across the whole vector.

**Fitness.** `popcount(~bad)` — the number of binary inputs the network sorts
correctly, out of 2^n. This is the gradient. A pass/fail fitness would give the
search nothing to climb; the count of correctly-sorted inputs is the standard
choice and is exactly computable here at no extra cost, since `bad` is already
being built.

### Cost, computed not guessed

| n | 2^n | per wire | all wires | word-ops per evaluation |
|---:|---:|---:|---:|---:|
| 12 | 4,096 | 512 B | 6 KB | ~4,900 |
| 16 | 65,536 | 8 KB | 0.12 MB | ~122,880 |
| **18** | **262,144** | **32 KB** | **0.56 MB** | **~630,784** |
| 20 | 1,048,576 | 128 KB | 2.50 MB | ~2,981,888 |

At n = 18 a full exhaustive correctness test of a 77-comparator candidate is
about 630,000 word operations — well under a millisecond, in 0.56 MB, which is
3% of a 16 MiB instance. n = 20 still fits; n = 24 (48 MB) does not.

## Search

Steady-state, not generational — it keeps every worker busy and avoids a
synchronisation point this platform has no cheap way to implement.

- **Selection:** tournament, size 4–7. Tournament rather than roulette because
  fitness here is a large count with small differences near the top, and
  roulette degenerates to near-uniform selection in that regime.
- **Crossover:** two-point on the comparator array. Both parents are valid
  networks of length L, and any splice of them is a valid network of length L —
  the representation is closed under crossover, which is why fixed length is
  worth the constraint.
- **Mutation:** replace a comparator with a random legal pair; swap two
  comparators' positions (order matters, so this is a real move); perturb one
  endpoint of a comparator.
- **Elitism:** keep the best 1–2 unchanged. Without it, steady-state search
  loses its best individual to drift.

**Prefix fixing** for the open target. The first layers are held to a
known-good prefix and only the remainder is evolved — the standard technique
for large n, and the one behind most modern improvements. *Before relying on
any specific "without loss of generality" claim about first layers, the exact
lemma must be checked against the literature.* Getting this wrong would mean
searching a space that provably cannot contain the answer, and the failure
would be silent.

## Validation gates, fixed before running

The harness must reproduce known answers before it is pointed at anything open.
These are exact published integers, so this is pass/fail, not judgement.

| gate | n | must find | budget |
|---|---:|---:|---|
| G1 | 4 | 5 comparators | 10^5 evaluations |
| G2 | 6 | 12 | 10^6 |
| G3 | 8 | 19 | 10^7 |
| G4 | 9 | **25** | 10^8 |

**G4 is the real gate.** Finding 25 for n = 9 is close to the frontier of what
evolution has historically achieved unaided, and a method that cannot reach it
has no business at n = 18. If G4 fails, the honest outcome is to report that
and stop, not to tune until it passes and call the tuning a result.

Each gate is run 10 times from different seeds; the gate is the median, not the
best, so a single lucky seed cannot carry it.

## Work partitioning

One worker = one independent population, a seed, and a generation budget. It
returns its best network and its fitness. Nothing shared, nothing to
coordinate, and a worker that dies costs one population.

This is deliberately the simplest thing that works. An island model with
migration is the obvious next step and is *newly possible* — migration needs
worker-to-worker communication, which was believed impossible here until the
~350-request inbound-serving claim was measured and retired (1200 churned
connections, zero accept failures). But migration is a second study with its
own question, and mixing it into this one would confound both.

Work-unit size is set by the granularity floor: enough generations that a
worker runs for minutes, not seconds.

## Metrics

- best fitness (inputs sorted correctly, of 2^n) against evaluations
- evaluations to first correct network, per gate, per seed
- whether the median seed clears each gate
- wall-clock per evaluation, to check the platform behaves as the cost table predicts

## Success criteria

- **Primary:** a correct 18-input network in ≤ 76 comparators. This would be a
  new upper bound.
- **Secondary, and the realistic one:** all four gates cleared, and a measured
  curve of evaluations-to-solution against n that says where this method runs
  out. A negative result with that curve attached is publishable within the
  project and is what most likely happens.
- **Failure that is still worth having:** G4 not cleared. That is a fact about
  the method on this hardware, obtained in an afternoon rather than a month.

## Out of scope

- Optimal *depth* networks. Different objective, different search, not mixed in.
- n > 20. n = 24 needs 48 MB of wire state and does not fit a 16 MiB instance.
- Proving optimality. Finding a 76-comparator network is an upper bound; proving
  no 76 exists is a SAT problem and a different discipline entirely.
- Variable-length genomes and parsimony pressure. Length is a run parameter here.

## Honest odds

Low. These bounds have been attacked for decades with better tools than three
16 MiB virtual machines, and thirty years of nobody improving n = 13 is the
relevant prior rather than an encouragement.

What makes it worth running anyway: the target is a tracked integer, validation
is exact at every step, the cost is knowable in advance because the arithmetic
above is all there is, and the method has priors on this exact problem rather
than borrowed ones.

## References

Optimal sizes and depths, the 2020 resolution of n = 11 and n = 12, and the
Juillé (1995) and Schwiebert (2001) results are from
<https://en.wikipedia.org/wiki/Sorting_network>. Before execution, the primary
sources for the n = 18 upper bound of 77 and for any first-layer normalisation
lemma must be located and read — the same discipline applied to the Betten
papers, where the cost figures we expected to cite turned out not to exist.
