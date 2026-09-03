# GMP on BareMetal

A port of GNU MP 6.3.0 to BareMetal-AppPort, so that big-integer work --
modular exponentiation, primality testing, prime-chain search -- runs on the
unikernel at something near native speed.

Written to be upstreamable: it follows AppPort's own `scripts/get-<lib>.sh` plus
`port/<lib>_port/` convention rather than inventing a parallel structure, and
GMP itself is vendored unmodified.

## Why this exists

[`../docs/portfolio/CUNNINGHAM-SIZE-RECORDS.md`](../docs/portfolio/CUNNINGHAM-SIZE-RECORDS.md)
costs out a Cunningham chain size record and reduces the whole substrate
question to one number:

> a Fermat PRP test at ~1,020 digits must cost **under 49 ms** on this platform
> to fit a $500 budget at 1x expectation, and **under 16 ms** for 3x coverage.

Measured native GMP with assembly is **8.6-9.1 ms** on an arm64 development
machine. Everything therefore turns on what fraction of that the port retains,
and `prp_bench.c` is the instrument that answers it.

## What was already there, and what this adds

The important discovery is that **there was no libc shim to write.**
BareMetal-AppPort already ships a patched musl 1.2.6, `crt0.c`, `posix_shim.c`,
`thread_shim.c` and a linker script `c.ld` -- along with ports of lwIP, mbedTLS,
curl, SQLite, libsodium, lwext4 and CPython 3.14. musl is built there with
exactly the flags GMP needs: `-mcmodel=large -mno-red-zone -fno-pic -fno-pie
-ffreestanding`.

So this is a library port, and a small one. GMP asks very little of an OS: its
`mpn` assembly is pure computation with no syscalls and no libm, and on the
computation path it reaches libc only for `malloc`/`free`/`realloc` and the
`mem*` family, all of which musl provides.

## The build, and why it is shaped this way

`scripts/build-gmp.sh` configures **hosted** and then recompiles underneath.

GMP is autotools, and configure runs link tests. Handing those `-nostdlib`
makes every test fail, whereupon configure concludes the compiler is broken and
gives up. But configure's job here is only to pick the CPU's assembly variant
and generate `config.h`/`gmp.h`, neither of which depends on which libc is
eventually linked. So configure runs against the host's headers with the
target's *codegen* flags, and `make` then rebuilds every object with
`-nostdinc -isystem $MUSL_INC` substituted in. AppPort already uses the same
trick for CPython.

Two flags in that script are load-bearing in ways that are invisible until they
fail:

- **`-U_FORTIFY_SOURCE`.** Debian's gcc enables `_FORTIFY_SOURCE` by default,
  which rewrites plain `memcpy`/`sprintf` calls into `__memcpy_chk`/
  `__sprintf_chk`. Those are glibc symbols; musl does not define them. The
  failure appears only at final link, as undefined symbols with no obvious
  connection to a hardening flag nobody set.
- **`--enable-fat`.** GMP's configure would otherwise detect the *build*
  machine's CPU, which is wrong twice over here: the build runs in an emulated
  amd64 container on arm64, and the target is a cloud metal instance that is
  not the build host either. Fat dispatches through `cpuid` at runtime.
  Confirmed selected: `WANT_FAT_BINARY 1`, path `x86_64/fat x86_64 generic`.

`GMP_ASM=0` builds `--disable-assembly`. That configuration is far too slow to
be useful in production -- it is the pass that isolates a toolchain problem from
an assembly or addressing problem when something breaks.

## Measuring

`prp_bench.c` times a Fermat PRP test on a modulus of the real record form
`m * p# * 2^j - 1`, rather than a random integer of the same length.

It times with **`RDTSC`, not `clock()`**. `clock()` never advances on this
platform ([`../docs/aco-r1/BAREMETAL.md`](../docs/aco-r1/BAREMETAL.md)) -- that
is what hung the first ACO build, and it is why the ACO driver takes a
compile-time iteration count rather than a seconds budget. Cycles are also the
better unit: they are independent of clock frequency, so a BareMetal number and
a Linux number taken on the same machine compare directly without either side
needing to know what time it is.

Parameters are compile-time (`-DPRP_PRIMORIAL=`, `-DPRP_REPS=`) because
BareMetal passes no `argv`, but `argv` is still read when present, so the
identical source runs on Linux for the side-by-side.

## Status

Built and verified on an arm64 Mac via an emulated linux/amd64 container.

| step | result |
|---|---|
| musl 1.2.6 `libc.a` for the target | 2,622,942 bytes |
| `libgmp.a`, assembly + fat, musl headers | 1,514,544 bytes |
| `prp_bench.app` linked with `c.ld` | **346,892 bytes** |
| undefined symbols in the linked ELF | **none** |
| GMP assembly present | `__gmpn_addmul_1` + `_atom`/`_bd1`/`_bt1`/`_core2` variants |
| known-answer selftest | **5 of 5 pass** |

Relocation census over every built GMP object -- the question that decides
whether the assembly can be kept at all:

| relocation | count |
|---|---:|
| `R_X86_64_64` | 4,759 |
| `R_X86_64_PC32` | 73 |
| `R_X86_64_PLT32` | 18 |
| **`R_X86_64_32` / `32S`** | **0** |

Zero 32-bit absolutes, so nothing truncates at `0xFFFF800000000000`. Sections
land at `.text @ ffff800000000000` (0x4f389), `.rodata`, `.data`, `.bss`, with
no *allocated* orphan sections -- only non-allocated `.comment`, which
`objcopy -O binary` drops.

`gmp_selftest.c` runs the same `libgmp.a` hosted on x86-64 and checks fixed
inputs against known answers: a Fermat test on the Mersenne prime 2^521-1
returning exactly 1, the same test on a composite *not* returning 1 (so a powm
stubbed to return 1 cannot pass), a 4096-bit squaring against its closed form,
a division round-trip at 3,400 bits, and a GCD whose asm path reaches
`ctz_table` through the RIP-relative addressing that would have broken here.
A library that linked but computed wrong is the worst outcome available on a
platform already known to miscompute once in 1.6 million operations, and it
would be near-impossible to attribute after the fact.

### One trap worth knowing before changing the build

The fat dispatcher emits two genuinely 32-bit-absolute jumps
(`mpn/x86_64/fat/fat_entry.asm:103,184`) that **would** fail to relocate here.
They are suppressed only because `PRETEND_PIC` is defined, and that happens
because the host triplet contains `linux`. Building the same tree with
`--host=x86_64-elf` -- which looks like the more correct choice for a
freestanding target -- turns the zero in that table into a hard
"relocation truncated to fit". The triplet is load-bearing, not cosmetic.

### A correction this work turned up, not yet confirmed by running

[`../docs/aco-r1/BAREMETAL.md`](../docs/aco-r1/BAREMETAL.md) concludes that
"BareMetal cannot time itself" and that throughput must be measured from
outside the VM. Reading `posix_shim.c` says that is too strong. `clock()` is
indeed dead -- musl routes it to `CLOCK_PROCESS_CPUTIME_ID`, which the
dispatcher rejects with `-EINVAL`, so it returns -1 forever. But
`clock_gettime(CLOCK_MONOTONIC)` **is** implemented, backed by
`b_system(TIMECOUNTER)` = nanoseconds since boot.

If that holds when run, the ACO driver could have taken a seconds budget after
all, and `prp_bench` can calibrate the TSC against a real nanosecond source
rather than assuming a clock rate. It is recorded here rather than edited into
that document because it comes from reading the port's source, not from
executing anything -- and that document reports a measurement.

## What is not done

**The port has not been run.** Docker Desktop exposes no nested KVM, so
Firecracker cannot start on this machine: the `.app` is built, linked and
inspected here, never executed. Everything above is a static property of the
artifact plus a hosted x86-64 correctness run.

The number the costing document actually needs -- milliseconds per PRP test on
BareMetal -- requires one session on a metal host. Until it exists, the
substrate verdict in
[`../docs/portfolio/CUNNINGHAM-SIZE-RECORDS.md`](../docs/portfolio/CUNNINGHAM-SIZE-RECORDS.md)
stays an estimate, and the 3.7x penalty stays an assumption carried over from a
64-bit-modular-arithmetic benchmark that is not this workload.

`scripts/run-metal.sh` is that session, written so it is a copy-and-run
rather than a debug loop. On an x86-64 metal host with a built BareMetal-App
checkout:

    ./gmp/scripts/run-metal.sh

It builds `libgmp.a`, re-runs the known-answer selftest on that machine's own
compiler, builds a **Linux control** from the same source against the same
`libgmp.a`, links and boots the unikernel, and prints the ratio between them
with the costing document's 49 ms / 16 ms bars alongside.

Both halves matter. The 3.7x penalty this repo carries was measured on 64-bit
modular arithmetic, not big-integer work, and applying it to GMP has been an
assumption since the costing document was written. One machine, one source, one
library, so the only variable left is the absence of an operating system.

Three things it handles that would otherwise cost a metal session each: it
refuses up front if `/dev/kvm` is missing or unreadable rather than failing
obscurely later; it pre-creates Firecracker's log file, because `baremetal.sh`
deletes that path and then passes it as `--log-path`, and Firecracker opens it
without creating it (reported upstream from this repo already); and it bounds
the boot with a timeout and captures the console either way, since a hung guest
cannot be asked whether it is finished.
