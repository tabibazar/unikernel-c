# The 7.2x was mostly our own contention

Ian Seyler, 2026-09-05: *"In the alpha of BareMetal Cloud all instances are
sharing the same vCPU. CPU intensive workloads will certainly suffer until our
infrastructure scales out in the next version."*

That is testable, and it tests cleanly.

## Measurement

Same worker binary, same 1011-digit operands, same instance type. The only
variable is how many of our own workers were running alongside it.

| condition | ms per candidate | source |
|---|---:|---|
| 3 workers + bmagent | 55.8 | w1, 53,500 candidates in 2,986 s |
| 3 workers + bmagent | 58.2 | w0 overnight, 295,887 candidates in 17,212 s |
| 3 workers + bmagent | 61.8 | w2, 46,700 candidates in 2,885 s |
| **1 worker + bmagent** | **19.2** | w0 alone, 47,000 candidates in 903 s |

**3.03x faster with two siblings removed.**

## What that means: throughput is perfectly conserved

| | aggregate candidates/sec |
|---|---:|
| 3 workers at 58.2 ms each | 51.55 |
| 1 worker at 19.2 ms | 52.08 |

**Identical within 1%.** Adding instances adds no throughput whatsoever — it
divides one vCPU more ways. This is about as clean a demonstration of a
conserved resource as a live system is likely to give.

The practical consequence: we now run **one** instance instead of three. Same
total work, one third the cost.

## The revised picture is much kinder to the platform

The 7.2x figure reported to Return Infinity was measured with three of our own
workers competing. A single tenant gets:

| | ms per candidate |
|---|---:|
| development Mac, one M-series core | 8.1 |
| BareMetal Cloud, one instance | 19.2 |

**2.4x, not 7.2x.** And an Apple M-series performance core is genuinely quick at
big-integer arithmetic, so 2.4x against one is a respectable number for a shared
alpha vCPU. The platform is not slow; it is oversubscribed, which is a different
problem with a different fix, and one they have already said is coming.

## Consequences for the ask

The instance-cap request is parked. More instances cannot help a CPU-bound job
while they share a vCPU — we would have been asking Return Infinity to grant
something that would have done nothing, and would then have had to explain why
it changed nothing. Worth revisiting when the infrastructure scales out.

## What this does not measure

Whether the vCPU is also shared with *other tenants*. Our three workers explain
a factor of three; the residual 2.4x against an M-series core could be the CPU
itself, other customers, or `bmagent`, which was running throughout both
conditions and is ours. Stopping `bmagent` would isolate the last of it, but it
is doing something the user wants and is not worth stopping for this.
