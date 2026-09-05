# Multiplier range allocation

Nothing enforces disjointness -- two workers given the same range simply spend
their time proving the same multipliers composite. So ranges are written down
here, and this file is the authority.

| owner | m from | m to | survivors | status |
|---|---:|---:|---:|---|
| local hunt (this Mac) | 1 | open-ended | -- | running, ~2.0e9/day, one core |
| cloud w0 | 100,000,000,000 | 100,105,000,000 | 295,887 | deployed 2026-09-05T00:52Z |
| cloud w1 | 100,105,000,000 | 100,210,000,000 | 295,909 | deployed 2026-09-05T00:54Z |
| cloud w2 | 100,210,000,000 | 100,315,000,000 | 295,284 | deployed 2026-09-05T00:54Z |

Instance ids: w0 `cmtno59v5009un1nbkj46erav`, w1 `cmtnocb3u00aln1nbpi8sc0pp`,
w2 `cmtnoceeh00asn1nbjdt39733`. The fourth slot holds `bmagent`, which was
already running and was deliberately left alone; the three stopped `aco-r3-*`
instances were deleted to make room, and their images are retained so they can
be recreated.

**The next free range is 100,315,000,000.** Take from there when redeploying.

## Why the cloud slices start at 1e11 and not 1e9

The first cloud slice was cut at m=1e9. That was wrong. The local hunt runs
open-ended from m=1 at roughly 2.0e9 multipliers per day, so it would have
reached 1e9 in about twelve hours and re-tested the cloud's range from scratch.
The duplication would not have produced a wrong answer -- just two machines
proving the same numbers composite, invisibly, for as long as it ran.

1e11 is fifty days of local progress away, which is longer than the credit
lasts and longer than the cap question will stay open.

If the local hunt is ever left running for months, move the cloud allocation up
again rather than letting it be overtaken.

## The 16 MiB cap bounds how much work one image can carry

A worker's multipliers are baked in, so the slice size is bounded by instance
RAM. That turns out to bind much sooner than expected:

| slice | survivors | run time | baked list |
|---|---:|---:|---:|
| 2e6 multipliers | 5,698 | ~1.0 min | 0.05 MB |
| 1.05e8 multipliers | ~300,000 | ~53 min | 2.4 MB |
| three days of work | ~24,700,000 | 72 h | ~198 MB |

198 MB does not fit in a 16 MiB instance, so **no single image can run for
three days**. A worker either finishes in under an hour and needs replacing, or
it has to obtain work some other way.

Two ways out, neither needed for a deployment proof but both needed for a real
hunt:

- **Sieve inside the worker.** A prime sieve to depth 1e7 needs ~1.25 MB and a
  segment bitmap another ~250 KB, which fits. Survival at 1e7 is about 1.26%
  against 0.28% at 1e9, so roughly 4.5x more PRP tests -- but the worker then
  runs indefinitely from a range rather than a list.
- **Fetch work over HTTPS.** The port has curl and mbedTLS, and outbound
  networking is the one thing these instances definitely do well.

For now slices are sized at ~300,000 survivors, about 53 minutes each, and
replaced as they finish.
