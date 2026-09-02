# R1 on BareMetal — the engine computes what Linux computes

Measured on one AWS `c5.metal` (Intel Xeon, 96 vCPU), Firecracker 1.7.0,
BareMetal-App at `SETUP_EXIT=0`. `pcb442`, 500 iterations, distance cache on,
2-opt on. The same amalgamated source file built two ways on the same machine:
`gcc -O2` for Linux, `./1-build.sh` for the unikernel.

| seed | Linux x86-64 | BareMetal x86-64 | identical |
|---|---:|---:|:--:|
| 1 | 52,214 | 52,214 | yes |
| 2 | 51,759 | 51,759 | yes |
| 3 | 51,984 | 51,984 | yes |
| 4 | 52,037 | 52,037 | yes |
| 5 | 52,201 | 52,201 | yes |

**Same-seed divergence on BareMetal: 0 of 5 pairs.** Each seed was also built,
booted and run twice; both runs returned the same tour every time.

**Cross-substrate divergence: 0 of 5.** Every tour BareMetal produced is the
tour Linux produced from the same source and seed.

The comparison is deliberately Linux-on-the-same-machine rather than the
development Mac. The Mac is arm64 with a different libc and compiler, so a
difference there would be uninterpretable -- architecture, not platform. Held
to one machine and one architecture, the only variable left is the absence of
an operating system.

## Why this was the expected result

It follows from [`../floatscope/`](../floatscope/), measured on the same host:
the interrupt-time register clobber reaches general-purpose registers, not SSE.
This workload is floating point end to end -- pheromone weights, the roulette
wheel, the hand-rolled root -- and the one integer quantity that matters, the
rounded distance, is provably insensitive to the root's last bits (see
[`../../aco/VALIDATION.md`](../../aco/VALIDATION.md)).

So the platform's known defect has no purchase on this workload, and the
measurement says so directly rather than by argument.

## Two platform constraints found the hard way

**BareMetal passes no `argv`.** The same reason it has no environment. The
first build fell through to the driver's `--seconds` default and hung.

**`clock()` never advances.** Every BareMetal run reports `ms=0`, so a
seconds-based budget cannot terminate at all -- that is what the hang actually
was. Both are now compile-time settings (`ACO_ITERS`, `ACO_SEED`), matching how
`cunningham.c` carries its worker id, and the driver refuses with
`ACO_ABORT reason=clock_does_not_advance` rather than spinning.

The consequence for measurement: **BareMetal cannot time itself.** Throughput
there has to be timed from outside the VM.

## Also fixed: baremetal.sh could not start Firecracker

`baremetal.sh start` removes `$FCLOG` and then passes it as `--log-path`.
Firecracker opens that file and does not create it, so it exited immediately
with `LoggerInitialization ... No such file or directory` and the VM never ran.
Adding a `touch "$FCLOG"` after the removal fixes it. This is in BareMetal-App
upstream, not in this repo -- worth reporting alongside the arithmetic fault.
