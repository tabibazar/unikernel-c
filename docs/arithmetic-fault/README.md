# The arithmetic fault, root-caused

BareMetal returns different answers to identical 64-bit modular arithmetic,
about once in four thousand operations, [measured earlier](../nanos-vs-baremetal/)
against a Linux control clean on the same host. This is the root cause, found by
narrowing the fault one experiment at a time. The reproducers and their raw
output are in this directory.

## The finding, in one paragraph

The fault is a **register-preservation bug in BareMetal's interrupt handling**.
The modular reduction `a * b % m`, compiled at `-O0`, becomes a call to libgcc's
software routine `__umodti3` (a 128&div;64 division built from many
instructions). When a maskable interrupt &mdash; in this configuration the timer
&mdash; lands while the CPU is inside that routine, the interrupt handler fails
to preserve a register the routine is using, and on return the routine finishes
its calculation with a corrupted intermediate and produces a wrong remainder.
The multiply never faults because it is a single `mulq` instruction, which is
atomic with respect to interrupts. The CPU is not at fault: Linux and Nanos run
the identical arithmetic cleanly on the same silicon, because their interrupt
handlers save and restore every register.

## How it was narrowed

Each step changed exactly one variable. Raw output is in [`results.txt`](results.txt).

### 1. Split the operation &mdash; which primitive faults?

[`faultscope.c`](faultscope.c) repeats one fixed `mulmod` 200 million times and
checks the 64&times;64&rarr;128 **multiply** and the 128&div;64 **modulo**
separately.

```
mul_bad=0   mod_bad=127   (rate 6.35e-7 per modulo)
```

The multiply is never wrong. Only the modulo faults. And the wrong remainder is
**byte-identical every one of the 127 times** &mdash; correct `b10537588efd4e70`,
wrong `bb77c5a9f2d3bb82`. A random hardware upset would give different garbage
each time; a deterministic wrong value means a specific mis-computation that
triggers intermittently. This also rules out corruption of the `__int128`
register pair itself, since the product living in that same pair is always
correct.

### 2. The instruction &mdash; why is the modulo special?

At `-O0` the modulo compiles to `call __umodti3`, a multi-instruction software
division. The multiply is a single `mulq`. A single instruction cannot be
interrupted partway; a software routine can. That is the only structural
difference between the operation that faults and the one that does not.

### 3. Mask interrupts &mdash; are they necessary?

[`faultscope_cli.c`](faultscope_cli.c) is identical but executes `cli` before
the loop, masking maskable interrupts for the whole run.

```
mod_bad=0   over 200,000,000 iterations
```

Zero, where the baseline predicts 127. Interrupts are necessary for the fault.
And because masking interrupts *in the guest* removes it, the cause is the
guest's own interrupt handling, not hypervisor preemption &mdash; `cli` cannot
stop a VM-exit, only a guest interrupt.

### 4. Make the reduction atomic &mdash; is interruptibility the vector?

[`faultscope_div.c`](faultscope_div.c) reduces the same product two ways in the
same loop, interrupts enabled: the software `__umodti3`, and a single inline-asm
`divq` instruction.

```
software_umodti3_bad=109   single_divq_bad=0
```

Same product, same loop, same interrupt environment. The interruptible routine
faults; the atomic instruction does not. This is the mechanism confirmed by a
controlled comparison rather than by inference.

### 5. Verify against the real reproducer

[`selfcheck_divq.c`](selfcheck_divq.c) is the original `selfcheck` with its
`powmod` rewritten to use the atomic `divq`. Interrupts enabled:

```
before (software modulo):  1398 mismatches / 20,000,000
after  (atomic divq):         0 mismatches / 20,000,000
```

The workaround fixes the real workload, not just the microbenchmark &mdash; and
runs slightly faster, one instruction beating a library call.

## What this means

**For Return Infinity.** The bug is almost certainly in the timer interrupt
service routine: it clobbers a register without saving and restoring it, and
that register is live inside `__umodti3` (and, by extension, any multi-
instruction sequence that spans an interrupt). The deterministic wrong value
`bb77c5a9f2d3bb82` for the product above is a fingerprint that can pin the exact
register once the ISR is compared against the `__umodti3` disassembly. The real
fix is to audit register preservation across the interrupt path; it will
silently affect far more than 128-bit division.

**For anyone running compute on BareMetal today.** Two workarounds, both making
the reduction a single atomic instruction:

- Compile hot modular arithmetic at `-O2` or higher, where gcc emits `divq`
  directly when it can prove the quotient fits in 64 bits (it does whenever the
  operands are already reduced, `a, b < m`).
- Or use an inline-asm `divq`, as [`selfcheck_divq.c`](selfcheck_divq.c) does.

Both close this particular hole. Neither is a substitute for the platform fix,
because the same interrupt-time clobber will corrupt any other multi-instruction
routine that happens to span an interrupt.

## Reproducing

On a BareMetal-App checkout, for each `.c` here:

```sh
cp faultscope.c BareMetal-App/ && ./1-build.sh faultscope.c
# then boot the resulting baremetal.elf under Firecracker (or ./2-run.sh)
```

`faultscope` and `faultscope_div` show the fault with interrupts enabled;
`faultscope_cli` shows it vanish with them masked. Every figure above is in
[`results.txt`](results.txt), taken from an AMD EPYC host under Firecracker 1.16.
