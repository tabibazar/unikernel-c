# AWS Lambda as a fan-out compute substrate — measured

us-east-1, `provided.al2023`, x86-64, 1769 MB (the point where Lambda allocates
a full vCPU). Workload is `../../gmp/prp_bench.c` — the same source the
BareMetal unikernel runs — statically linked against the same `libgmp.a`.
Measured 2026-09-03.

## Cold start: 39–45 ms, not 950 ms

| | Init Duration |
|---|---:|
| cold 1 | 33.97 ms |
| cold 2 | 36.42 ms |
| cold 3 | 70.59 ms |
| cold 4 | 38.17 ms |
| **mean** | **44.8 ms** |

**This corrects an assumption this repo was about to publish.** The
`nanos-vs-baremetal` measurement of a Linux guest booting under Firecracker in
0.95 s is a real number, and it is the wrong comparator for Lambda. Lambda does
not cold-boot a Linux kernel per invocation — it restores pre-initialised
microVM snapshots — so its cold start is 39–45 ms against BareMetal's 31 ms.

BareMetal is roughly **1.3x faster to first instruction**, not 30x. Anyone
pitching boot latency against Lambda on the strength of the 0.95 s figure is
quoting a number that does not apply.

## Throughput: the two substrates compute alike

| | cycles per PRP test |
|---|---:|
| Lambda, warm, n=4 | 33.27M, 33.27M, 33.29M, 33.39M |
| spread | **0.4%** |

Cycle counts are extremely stable across invocations — no evidence of noisy
neighbours at this memory size on this workload.

For scale, the same binary on an arm64 development machine takes ~8.6 ms at
roughly 4 GHz, i.e. ~34M cycles. **Per-cycle efficiency is essentially
identical.** The substrates are not distinguished by how well they compute;
they are distinguished by what a cycle costs.

## Cost: this is the whole story

| | $ per core-hour | vs BareMetal |
|---|---:|---:|
| **AWS Lambda** (1769 MB, list price) | **$0.1037** | 20.7x |
| BareMetal Cloud | $0.00501056 | 1x |

Per unit of work — one expected Cunningham k=6 chain, 7.3e9 PRP tests:

| substrate | cost |
|---|---:|
| Lambda | **$2,848** |
| BareMetal at today's 3.7x compute penalty | ~$509 |
| BareMetal with that penalty fixed | ~$138 |

So BareMetal is **5.6x cheaper per unit of work even while 3.7x slower**, and
~20x cheaper once the penalty is gone. The price gap is large enough to absorb
a substantial performance handicap, which is the same arithmetic that decided
the chain-hunt budget.

## What this measurement does not say

- **List price, no commitment.** Lambda has no spot market and no committed-use
  discount applied here. EC2 spot at roughly $0.01–0.02/vCPU-hour is the
  tougher comparator for batch compute, and Lambda is not priced for batch —
  it is priced for event-driven work, which is a different product.
- **21% of the billed time is not compute.** At 200 reps the handler spends
  11.13 ms per test computing and 13.52 ms per test billed; the difference is
  the runtime-API round trip, the shell bootstrap and one warm-up test. A
  production handler amortises further, so Lambda's true cost per unit work is
  a little better than the table shows.
- **Whether $0.00501056/hr is comparable.** That is the operator's own figure.
  Whether it is list price, cost price, or a rate that survives contact with a
  paying customer is not something this benchmark can establish.
- **Nothing about isolation, tenancy or security posture**, which is most of
  what the named competitors actually sell.

## Reproducing

```sh
GMP_SRC=/path/to/gmp-6.3.0 ./build.sh   # static x86-64 binary
./deploy.sh                              # one IAM role, one function
./measure.sh                             # cold starts, warm runs, cost table
./teardown.sh                            # removes both
```
