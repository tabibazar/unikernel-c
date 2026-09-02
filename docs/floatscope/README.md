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
hardware instruction bit for bit — which also establishes that the hand-rolled
Newton root in `aco.c` is correctly rounded, not merely close.

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

**Pending.** Requires a Linux host with a BareMetal-App checkout; see
[Reproducing](#reproducing). No provisional numbers are recorded here.

Either outcome is worth having. Clean narrows the existing bug report to the
integer path. Dirty extends it materially — and would mean the platform
corrupts floating-point arithmetic, which no measurement here has yet shown.

## Reproducing

```sh
cp floatscope.c     BareMetal-App/ && ./1-build.sh floatscope.c
cp floatscope_cli.c BareMetal-App/ && ./1-build.sh floatscope_cli.c
# then boot each resulting baremetal.elf under Firecracker
```

`floatscope_cli` cannot run on the host: `cli` is privileged and faults in user
space. It is built, not run, and refuses to compile off x86-64.
