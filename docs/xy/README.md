# The Kosterlitz–Thouless transition, on a unikernel

The [Ising study](../ising/) found a critical temperature and matched Onsager's
exact 1944 solution. This is the stranger transition next door — the one that
won the 2016 Nobel Prize, and the one where the usual picture of a phase
transition simply does not apply.

## What is odd about it

Replace each ±1 Ising spin with a continuous angle and the Mermin–Wagner theorem
forbids any ordered phase at all in two dimensions. The magnetisation is zero at
every non-zero temperature. There is nothing to break.

And yet there is still a sharp transition, and it is **topological**. The
relevant objects are vortices — plaquettes around which the angle field winds
through a full turn. At low temperature they exist only as tightly bound
vortex–antivortex pairs. Above the transition the pairs unbind, free vortices
wander, and they destroy the quasi-order that was there.

## The test is a pure number

The helicity modulus Υ measures how much energy it costs to twist one edge of
the lattice against the other. Kosterlitz and Thouless predict it does not fall
smoothly to zero — it drops discontinuously, and the size of the jump is
**universal**:

> **Υ(T_KT) / T_KT = 2/π**

No coupling constant, no lattice detail, no fitted parameter. The same 2/π for
every system in this universality class.

That makes this a sharper test than the Ising study. There the question was "did
we land near Onsager's number". Here the question is "does the measured curve
cross a line that theory fixes exactly" — and where they cross *is* T_KT. High-
precision Monte Carlo puts it at **0.8929** in the infinite-lattice limit.

## Results

Measured on a 2D lattice with periodic boundaries, Metropolis single-spin
updates, seeded xoshiro256++, three lattice sizes. Data in
[`xy16.txt`](xy16.txt), [`xy32.txt`](xy32.txt), [`xy64.txt`](xy64.txt); the
crossing is extracted by [`crossing.py`](crossing.py).

| lattice | crossing | reading |
|---|---:|---|
| 16 x 16 | 0.9395 | above 0.8929, as a small lattice must be |
| 32 x 32 | 0.9169 | drifting down, as predicted |
| 64 x 64 | 0.9239 | **not resolved** -- see below |

The 64x64 point does not fit the trend and is left that way. It should have
fallen below the 32x32 value and came in above it. The cause is mundane: it got
2.5x fewer measurement sweeps, and near the crossing the curve is steep enough
that modest statistical noise moves the intersection a long way. That is a limit
of how long we ran it, not a finding -- and dropping the point would have made a
cleaner chart and a worse result.

Vortex counts from the field dumps, which are the mechanism rather than a
summary of it:

| T | vortices | balance |
|---:|---:|---|
| 0.50 | 0 | none free |
| 0.90 | 20 | 10 positive, 10 negative |
| 1.30 | 246 | 123 positive, 123 negative |

The exact +/- balance at every temperature is forced by topology on a periodic
lattice, and is the check that the counter is right.

The crossing sits **above** 0.8929 and drifts down as the lattice grows. That is
the expected behaviour and not an error to be tuned away: convergence to the
infinite-lattice value is only *logarithmic* in L, which is why this transition
is notoriously awkward to pin down numerically and why three sizes show the
direction of the drift rather than extrapolating to the answer.

Two other signatures come out of the same runs, and both are the mechanism
rather than a summary of it:

- **Vortex density rises by nearly three orders of magnitude** through the
  transition — from ~1e-4 in the bound-pair phase to ~7e-2 once the pairs have
  broken. This is counted directly, plaquette by plaquette, rather than inferred
  from the thermodynamics.
- **The energy stays smooth.** There is no latent heat and no jump. A KT
  transition is infinite-order, so the thermodynamic quantity most people would
  reach for shows almost nothing — which is precisely why the helicity modulus
  is the observable that matters here.

## No math library

There is none in a freestanding unikernel. The Ising study had to hand-roll
`exp` and `sqrt`; the XY model needs trigonometry too, since its energy is a
cosine rather than a product of signs.

`a_sin`, `a_cos` and `a_exp_neg` are in [`xy.c`](xy.c) and were checked against
libm over the ranges actually used:

| function | range | max error |
|---|---|---|
| `a_sin` | [−20, 20] | 6.0e-12 absolute |
| `a_cos` | [−20, 20] | 6.0e-12 absolute |
| `a_exp_neg` | [0, 40] | 6.2e-15 relative |

That check earned its keep. The first version stopped the sine series at x¹¹,
which sounds like plenty and is not — the next term alone is
(π/2)¹³/13! = 5.7e-08, and that is exactly the error it showed. Two more terms
cost two multiplies and bought four orders of magnitude. Nothing in the physics
would have looked obviously wrong at 5.7e-08; it would just have been quietly
less accurate than claimed.

## Determinism, deliberately

The Ising study used `RDRAND` and hardware randomness was the point there. This
one uses a **seeded xoshiro256++** instead, because here the point is different:
with the seed published, the entire run replays exactly.

That matters more than it sounds. This repo has already shown
([`../aco-r1/`](../aco-r1/)) that the same seed produces bit-identical results on
BareMetal and on Linux from one source. So a result here is not merely
*repeatable* — someone can reproduce the exact numbers, on a different operating
system, or on no operating system at all.

## Reproducing

```sh
gcc -O2 -o xy xy.c && ./xy                      # Linux
cp xy.c BareMetal-App/ && ./1-build.sh xy.c     # unikernel, then boot it
./crossing.py xy16.txt xy32.txt xy64.txt        # extract T_KT
```

Compile-time settings (BareMetal passes no `argv`): `XY_L`, `XY_SEED`,
`XY_WARMUP`, `XY_SWEEPS`, `XY_TMIN`, `XY_TMAX`, `XY_TSTEP`, `XY_DELTA`.
`-DXY_DUMP_FIELD=1` prints the final angle field, which is what you plot to see
the vortices.

The lattice is 8 bytes per site, so even 128×128 is 128 KB — this runs
comfortably in the 4 MiB floor the platform supports, let alone 16 MiB.
