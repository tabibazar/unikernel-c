# How many inbound requests can BareMetal actually serve?

Two documents in this repo state that **"inbound serving fails at ~350
requests"** — [`../docs/superpowers/specs/2026-09-02-aco-swarm-design.md`](../docs/superpowers/specs/2026-09-02-aco-swarm-design.md)
and [`../docs/portfolio/PROPOSAL.md`](../docs/portfolio/PROPOSAL.md) — both
saying it was "established separately". **There is no logged run behind it
anywhere in the tree.**

Every other constraint here has a measurement attached. This one does not, and
it is load-bearing: it is the single fact that decides whether any
request-serving workload can live on this platform.

## The question is what kind of limit, not what number

| kind | consequence |
|---|---|
| **hard cap** | something exhausts permanently. Nothing that accepts requests can run here. |
| **churn limit** | a reclaimable resource runs out and returns after a pause. A keep-alive or pooled client may never reach it, and the workload becomes viable. |
| **concurrency cap** | the port's own tables — `SOCK_MAX` is 16, `ACCEPTQ_MAX` is 8 (`port/net_shim.c`). These bound *simultaneous* sockets, so they cannot by themselves explain a *cumulative* failure near 350. |

## The standing hypothesis

`port/lwip_port/lwipopts.h` sets neither `MEMP_NUM_TCP_PCB` nor
`MEMP_NUM_TCP_PCB_TIME_WAIT`, so lwIP's defaults apply — five apiece. And
`webserver.c`, the port's own example, answers `Connection: close` and closes
per request, so **every request mints a TIME_WAIT PCB**.

If that is the mechanism, then keep-alive should sail past whatever number
churn finds, and the limit should heal after a pause. The benchmark is built to
falsify that.

## Three phases

1. **Churn** — one connection per request, the condition `webserver.c` creates
   and presumably the one the original claim was measured under.
2. **Recovery** — stop, wait longer than 2×MSL, try again. If serving resumes,
   the ceiling drains and is not a cap. *This is the phase nobody has run, and
   it decides the question.*
3. **Keep-alive** — many requests down one connection.

The guest counts what it served, the driver counts what came back, and the two
disagreeing is itself a finding: a request the client calls failed but the
server handled is a different fault from one that never arrived.

**A failed `accept()` does not end the run.** What happens *after* the first
failure is the interesting part, and a program that exits there can only ever
report a hard cap.

## One trap already caught

The first self-test reported **40 of 40 successes** and cheerfully printed
"retires the ~350 claim". The benchmark had failed to bind (`EADDRINUSE`) and
`curl` was talking to an unrelated service on the same port.

The driver now identifies the listener before measuring anything — every reply
from `inbound_bench` carries `served=` in its body — and refuses to run against
anything else. A load generator that does not check what answered is not
measuring the thing it names.

## Running it

```sh
# guest
cp inbound_bench.c BareMetal-App/ && ./1-build.sh inbound_bench.c
./baremetal.sh start

# driver, from the host
./drive.sh <guest-ip> 8080

# keep-alive variant needs a rebuild, since BareMetal takes no argv
gcc -DBENCH_KEEPALIVE=1 ...
```

Verified end to end against the Linux control build: 60 of 60 churned requests
served, guest and driver counts agreeing.
