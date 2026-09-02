# R1 — one colony, one host

The control for everything that follows. Every number here is from a
general-purpose OS on known-good hardware; none of it is a finding about
BareMetal. Its job is to establish what "correct" looks like so that a
deviation on BareMetal can be attributed.

Host: Apple arm64, clang `-O2 -std=c99`, one core, distance cache on.
500 iterations per run, two runs per seed, five seeds per instance.

| instance | runs | median gap | iters/s | divergent pairs |
|---|---:|---:|---:|---:|
| kroA100 | 10 | 5.33% | 5,394 | 0 of 5 |
| pcb442 | 10 | 21.01% | 906 | 0 of 5 |

## Same-seed divergence: 0 of 10 pairs

Each seed is run twice with an identical iteration count, so the two runs
perform exactly the same work on exactly the same input. On this host they
returned bit-identical tours every time.

That zero is the point of the rung. The engine is deterministic by
construction — a seeded xoshiro256++, no wall-clock in the search, no
concurrency — so on a platform that computes correctly the rate must be zero.
When the same script runs on BareMetal, a **non-zero rate is a platform
measurement**, and it has a named suspect: the interrupt-time register clobber
already root-caused in [`../arithmetic-fault/`](../arithmetic-fault/), whose
floating-point reach is being probed separately in
[`../floatscope/`](../floatscope/).

A non-zero rate *here* would instead be a bug in this code, and would have to
be fixed before the BareMetal run meant anything.

## Two ways this measurement was nearly faked

Both were caught, and both produced a confident, wrong "0 divergent":

- **The field extraction silently matched nothing.** The first driver used
  `sed` with `\?`, a GNU extension absent from BSD sed, so every field came
  back empty and the divergence check compared empty strings to empty strings.
  Fields are now pulled with `awk`, and the script aborts if a line fails to
  parse.
- **An equivalence check ran below the search's floor.** A related check at 200
  iterations compared runs that had not yet improved on the nearest-neighbour
  starting tour, which is seed-independent — so every run returned the same
  number for the wrong reason. Checks now run at an iteration count where seeds
  demonstrably produce different tours.

A determinism experiment reports zero both when nothing diverged and when
nothing was measured. The two have to be told apart deliberately.

## Pending

The BareMetal half needs a Linux host with a BareMetal-App checkout. `run.sh`
is written to run there unchanged.
