# Does the interrupt path preserve SSE state?

[The arithmetic fault](../arithmetic-fault/) was root-caused to a register the
interrupt handler fails to preserve, live inside libgcc's `__umodti3`. It was
proven by contrast: the single-instruction `mulq` was never wrong, the
multi-instruction software routine was, and masking interrupts made the fault
vanish.

Every operation in that investigation was **integer**. Whether the same
handler also clobbers **XMM** registers was never asked. It matters now,
because the ant-colony workload ([`../../aco/`](../../aco/)) is floating point
end to end, and its `a_sqrt` is exactly the shape that faulted: a
multi-instruction software routine holding a live intermediate.

## The experiment

One register file over, the same design as `faultscope`:

| | operation | form | expectation |
|---|---|---|---|
| `mul` | `a*b` | single `mulsd` | clean — atomic |
| `div` | `a/b` | single `divsd` | clean — atomic |
| `sw_sqrt` | Newton–Raphson | ~25 instructions | **the suspect** |
| `hw_sqrt` | `sqrtsd` | single instruction | the control for `sw_sqrt` |

`sw_sqrt` and `hw_sqrt` compute the same value by different means, one
interruptible and one not. That pairing is what turns a fault count into a
mechanism, exactly as `faultscope_div.c` did for `__umodti3` against `divq`.

[`floatscope_cli.c`](floatscope_cli.c) is identical with `cli` before the
loop. If a fault vanishes under it, the cause is the guest's own interrupt
handling: `cli` cannot stop a VM-exit, only a guest interrupt.

## Host control

The probe must be silent on a machine known good, or it proves nothing:

```
SCOPE_START hw_sqrt_is_real=1 iters=200000000 mul=4011c5831add62e4
            div=3fdccf6429be6622 sw_sqrt=4000dccdf82c9163 hw_sqrt=4000dccdf82c9163
SCOPE_DONE  iters=200000000 mul_bad=0 div_bad=0 sw_sqrt_bad=0 hw_sqrt_bad=0
```

Clean over 200,000,000 iterations on macOS/arm64, and `sw_sqrt` agrees with the
hardware instruction bit for bit **on this input**. That is agreement on one
value, not a proof of correct rounding everywhere — see
[`../../aco/VALIDATION.md`](../../aco/VALIDATION.md) for why the distances are
exact regardless of how many ulps `a_sqrt` is off.

Two properties were checked rather than assumed:

- **The loop really recomputes.** Runtime is linear in iterations
  (20M → 0.33 s, 80M → 0.75 s, 200M → 1.62 s). Inputs are `volatile`, so
  every result depends on a fresh load and no hoisting is legal.
- **The probe would catch a fault.** With the reference value deliberately
  corrupted by one bit, it reports `sw_sqrt_bad=100000` out of 100,000 — it
  detects a wrong result on every iteration, so a real fault cannot slip past.

`hw_sqrt_is_real` is printed because on a target with no hardware square root
the control degenerates into `sw_sqrt` and the comparison becomes worthless.
It must read `1` for the result to mean anything.

## Results on BareMetal

**The interrupt-time corruption does not reach SSE state.** Measured on an
AWS `c5.metal` (Intel Xeon) under Firecracker 1.7.0, BareMetal-App at
`SETUP_EXIT=0`:

```
SCOPE_START hw_sqrt_is_real=1 iters=2000000000 mul=4011c5831add62e4
            div=3fdccf6429be6622 sw_sqrt=4000dccdf82c9163 hw_sqrt=4000dccdf82c9163
SCOPE_DONE  iters=2000000000 mul_bad=0 div_bad=0 sw_sqrt_bad=0 hw_sqrt_bad=0
```

Two billion iterations, 100 seconds of wall time, not one deviation — in any of
the four operations, including the multi-instruction software square root that
is structurally the same shape as the `__umodti3` routine that does fault.

### The positive control, which is what makes the zero mean anything

A null result is worthless without evidence that interrupts were firing during
it. So the original integer probe was rebuilt and run **on the same host, same
boot, same Firecracker**:

| probe | operation | iterations | wall | faults | rate |
|---|---|---:|---:|---:|---:|
| `faultscope` | 128/64 integer modulo | 200,000,000 | 15 s | **37** | 1.85e-7 |
| `floatscope` | FP mul, div, sw sqrt, hw sqrt | 2,000,000,000 | 100 s | **0** | < 1.5e-9 |

The integer fault reproduced at 2.5 faults per second of exposure. Had the
floating-point path been equally vulnerable, the 100-second run should have
produced roughly 250 of them. It produced none. The 95% upper bound on the FP
rate is 1.5e-9 per iteration, at least **80x below** the integer rate measured
beside it.

### And the fingerprint travelled

The August measurement was taken on an **AMD EPYC** host. This one is an
**Intel Xeon**, and the corrupted remainder is byte-identical:

```
correct  b10537588efd4e70
wrong    bb77c5a9f2d3bb82     (xor 0a72f2f17c2ef5f2)
```

The same wrong value on different silicon, from a different vendor, is strong
evidence for the software diagnosis already reached in
[`../arithmetic-fault/`](../arithmetic-fault/): a specific register the
interrupt handler fails to preserve, not a hardware erratum.

### What this means

**For Return Infinity.** The bug report narrows: the unpreserved register is in
the general-purpose file, and the interrupt path evidently does not disturb
XMM state. That is consistent with an ISR that never touches SSE, and it should
help pin the exact register against the `__umodti3` disassembly.

**For running compute on BareMetal today.** Floating-point work is not exposed
to this defect. That is what makes the ant-colony workload viable here, and it
is the measurement the rest of that study rests on.

The `cli` control was not run. It exists to make a fault *vanish*, and there
was no fault to remove.

## Reproducing

```sh
cp floatscope.c     BareMetal-App/ && ./1-build.sh floatscope.c
cp floatscope_cli.c BareMetal-App/ && ./1-build.sh floatscope_cli.c
# then boot each resulting baremetal.elf under Firecracker
```

`floatscope_cli` cannot run on the host: `cli` is privileged and faults in user
space. It is built, not run, and refuses to compile off x86-64.
