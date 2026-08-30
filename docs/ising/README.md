# The Ising phase transition by MCMC, on a unikernel

Plain Monte Carlo estimates a volume by throwing random points — easy. The hard
version is sampling from a distribution you cannot draw from directly. This does
that: it finds the critical temperature of the 2D Ising model by Markov Chain
Monte Carlo, on a BareMetal unikernel, driven entirely by the CPU's hardware
randomness — and it matches Lars Onsager's exact 1944 solution.

## The result

- **Onsager, exact (1944):** Tc = 2 / ln(1 + √2) = **2.269185…**
- **Our MCMC, 64×64 lattice:** magnetic susceptibility peaks at **T = 2.25**,
  the expected finite-size shift above the infinite-lattice value.

The magnetisation tracks Onsager's exact curve through the ordered phase
(0.9970 vs 0.9970 at T=1.2) and collapses at the transition. Data in
[`curve.tsv`](curve.tsv).

## Why it is hard

You cannot sample spin configurations uniformly — the physically real ones are
exponentially weighted by energy, so uniform sampling almost never lands on one.
Metropolis–Hastings ([`ising.c`](ising.c)) walks a Markov chain instead:
propose a single spin flip, accept it with probability `min(1, exp(−ΔE/T))`, and
over enough steps the chain visits each configuration in proportion to its
Boltzmann weight. Every accept/reject is one hardware random number from
`RDRAND`, [proven clean the day before](../trng/).

The three phases, straight off the machine:

- **T = 1.6** ordered — nearly all spins aligned, a magnet.
- **T = 2.27** critical — clusters of every size, a fractal; the fingerprint of
  a critical point.
- **T = 3.2** disordered — thermal noise wins, no large-scale order.

## Also here

[`montecarlo.c`](montecarlo.c) is the easy baseline the hard version grew from:
volumes of a sphere (known 4/3π, to validate) and an irregular metaball blob
(no closed form) by plain rejection sampling — 400M hardware-random points,
metaball volume 0.6856 ± 0.0001, with the error shrinking as 1/√N exactly as
theory says.

## Reproducing

```sh
cp ising.c BareMetal-App/ && ./1-build.sh ising.c     # then boot under Firecracker
```

Measured on an AMD EPYC host under Firecracker 1.16. The exp() and sqrt() are
hand-rolled — there is no math library in a freestanding unikernel.
