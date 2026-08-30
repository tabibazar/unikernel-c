# Nanos and BareMetal, measured on one machine

Two of the twelve platforms in the [competitive landscape](../competitor-landscape.html)
actually run unikernels. This is a head-to-head between one of them and
BareMetal, on a single host, with the same C source and the same measurements
on both sides.

Everything here is reproducible from the scripts in this directory. Nothing in
it is a vendor claim.

## The host

One machine, both guests: `bmhunt`, an AMD EPYC box with 2 shared vCPU, 8 GB
and hardware KVM. Comparing across two clouds measures the clouds, so there is
only one.

| | version |
|---|---|
| host kernel | Linux 6.1.0+ x86-64 |
| gcc | 13.3.0 (Ubuntu 24.04) |
| QEMU | 8.2.2 |
| Firecracker | 1.16.1 |
| ops / Nanos | 0.1.46 / 0.1.55 |
| BareMetal | BareMetal-App + BareMetal-AppPort, Aug 2026 checkout |

Each guest runs under the VMM its own platform ships with, because neither
would boot under the other's. BareMetal's ELF has no PVH note, so QEMU's
microvm refuses it (`Error loading uncompressed kernel without PVH ELF Note`);
it runs under Firecracker, which is also what BareMetal Cloud runs. Nanos boots
from a disk image with its own loader under QEMU/KVM, which is what `ops` runs.
Both get `-smp 1`, 512 MiB, user-mode networking, and a serial console piped
through a timestamper. **That difference is the main caveat on the boot
column** — and it is why the Linux control below matters.

## What was measured

Same two programs everywhere, byte-identical source:

- **`selfcheck.c`** — the same modular exponentiation 20 million times and a
  count of how often the answer differs from the first one. On a correct
  machine the count is zero by construction.
- **`bench.c`** — Miller-Rabin over a fixed candidate range, 40 million
  candidates. Fixed work, so elapsed time is the whole result.

Five runs of everything; medians below, spreads in `results.tsv`. Two shared
vCPUs make any single timing a statement about scheduling luck.

BareMetal's `build-app.sh` passes no `-O` flag, so its app is compiled at
`-O0`. The Linux and Nanos binaries are therefore built **both** ways: `-O0` is
the matched comparison, `-O2` is what those platforms give a user who does
nothing special.

## Results

*(filled in below from `results.tsv` — see `analyze.py`)*

### Boot: BareMetal wins by an order of magnitude

Seconds from the VMM process being execed to the application's own first
printed line. Medians of five.

| | boot → first app instruction |
|---|---|
| **BareMetal** under Firecracker | **0.031 s** |
| Nanos under QEMU/KVM | 0.240 s |
| Linux under the same Firecracker | 0.95 s |
| Linux, native process (no VM) | 0.027 s |

BareMetal reaches its application about **8× faster than Nanos** and **31×
faster than a Linux guest under the same hypervisor** — and it is within noise
of the cost of starting an ordinary process on the host, with a whole virtual
machine in between.

NanoVMs publishes "~60 ms" for boot. That is defensible as raw kernel time —
Nanos has its network up at about 0.19 s — but what a user waits for before
their code runs is nearer 0.24 s, four times the headline. Return Infinity
publishes no boot figure at all, and would win this column decisively if it did.

### Memory: BareMetal runs where Nanos will not boot

Smallest memory the same trivial program will run in, walked down until it
stopped working.

| | smallest working | first failure |
|---|---|---|
| **BareMetal** | **4 MiB** | 3 MiB |
| Nanos | 16 MiB | 12 MiB |

For scale, the smallest unit any competitor in the landscape survey will sell
is 128 MB.

### Image: the whole BareMetal artifact is smaller than the Nanos kernel

| | bytes |
|---|---|
| **BareMetal, complete bootable ELF** (kernel + app + musl + lwIP + mbedTLS + curl) | **1,323,504** |
| Nanos `kernel.img` alone | 1,792,016 |
| Nanos bootable disk image | 14,182,912 (3.8 MB actual, sparse) |
| Linux `vmlinux` used for the control | 20,176,896 |

### Compute: BareMetal is 3.7× slower

Fixed work, identical source, medians of five.

| platform | selfcheck 20M | bench 40M |
|---|---|---|
| Linux `-O2`, native | 12.85 s | 12.25 s |
| Nanos `-O2` | 12.91 s | 12.28 s |
| Linux `-O0`, native | 14.01 s | 14.92 s |
| Nanos `-O0` | 14.06 s | 15.20 s |
| Linux `-O0` under Firecracker | 14.14 s | — |
| **BareMetal** | **51.68 s** | **51.13 s** |

Nanos costs essentially nothing against native Linux — under 1% at either
optimisation level. BareMetal costs 3.7×.

**It is not the missing `-O2`.** That was the obvious explanation and it is
wrong: on this workload `-O0` versus `-O2` is 14.01 s against 12.85 s, about
8%. It is not the hypervisor either, because Linux under the same Firecracker
lands at 14.14 s. Something in the BareMetal port is paying 3.7× for 64-bit
modular arithmetic, and after this experiment the cause is still not isolated —
only three plausible causes are eliminated.

### Determinism: the result that needed a control

`selfcheck` computes one fixed modular exponentiation 20 million times. The
inputs never change, so a correct machine returns the same answer 20 million
times and the mismatch count is zero by construction.

| platform | mismatches per 20M, five runs |
|---|---|
| Linux, native | 0, 0, 0, 0, 0 |
| Nanos (`-O0` and `-O2`) | 0, 0, 0, 0, 0 |
| **Linux under the same Firecracker** | **0, 0, 0, 0, 0** |
| **BareMetal** | **1393, 1399, 1324, 1361, 1362** |

That third row is the point of the whole exercise. BareMetal ran under
Firecracker and Nanos under QEMU, so on their own the first two rows leave the
hypervisor confounded with the kernel. Booting an ordinary Linux guest under
the *same* Firecracker, on the same host, with the same binary at the same
optimisation level, removes that: it comes back clean. **The nondeterminism is
BareMetal's** — not the hardware's, not KVM's, not Firecracker's.

**The cause is now found.** A follow-up investigation root-caused it: a
maskable timer interrupt landing inside libgcc's software `__umodti3`
(the 128÷64 division the modulo compiles to at `-O0`) corrupts a register
BareMetal's interrupt handler fails to preserve. Masking interrupts drives it to
zero; replacing the software routine with a single atomic `divq` drives it to
zero with interrupts on. Full write-up and reproducers in
[docs/arithmetic-fault](../arithmetic-fault/).

It is also worse here than where it was first caught: a rate of 6.8 × 10⁻⁵
against 1.6 × 10⁻⁶ measured earlier, roughly forty times more frequent on this
AMD EPYC host.

### It returns wrong answers, not just a warning counter

`bench` counts primes in a fixed range. Every run must produce the same count.
Five BareMetal runs:

```
primes=4267876   primes=4267777   primes=4267803   primes=4267888   primes=4267838
```

Five runs, five different answers. Linux and Nanos return **4268930** every
time. The error is one-directional — BareMetal only ever undercounts, so the
corruption is causing Miller-Rabin to reject genuine primes rather than accept
composites, which is what you would expect if a value is being disturbed
between a multiply and its comparison.

About 1,100 primes in 4.27 million go missing, so roughly one result in four
thousand is wrong, with nothing printed to say so. On a platform whose stated
vertical is deterministic scheduling for financial services, this is the
finding that matters most, and it is why it was tested first.

## Reproducing

On a host with KVM, Firecracker, QEMU, `ops`, and a BareMetal-App checkout:

```sh
bash driver.sh            # the 5x5 matrix -> results.tsv
python3 analyze.py results.tsv
bash minram.sh            # the memory floor for each platform
./fcctl.sh /tmp/ctl.log   # the Linux-under-Firecracker control
```

`harness.sh` holds the two launchers and the serial timestamper; `fcrun.sh`
boots BareMetal under Firecracker; every raw serial log is in `logs/`.

## What this does not measure

No TLS or HTTPS comparison: this host has no TUN driver, so a Firecracker guest
here has no network at all, and BareMetal could not be given one. The 888 ms
program-start-to-completed-HTTPS figure for BareMetal was measured on BareMetal
Cloud, on different hardware, and is not comparable to anything here.

Nanos was run locally through `ops`, not on NanoVMs' hosted product, so nothing
here evaluates their cloud — only the kernel. Two shared vCPUs also make this a
poor absolute-throughput host; the ratios are the result, not the seconds.
