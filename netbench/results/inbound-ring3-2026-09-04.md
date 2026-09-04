# Inbound serving on BareMetal: the ~350 claim, measured

`docs/superpowers/specs/2026-09-02-aco-swarm-design.md` and
`docs/portfolio/PROPOSAL.md` both state that **inbound serving fails at ~350
requests**, each saying it was "established separately". No logged run existed
anywhere in the tree. This is that run.

## Setup

| | |
|---|---|
| host | AWS c5.metal, Xeon Platinum 8275CL @ 3.00GHz, 96 cores, 188 GiB |
| build | BareMetal-App `82ad9a7`, BareMetal-AppPort `292e4ae` ("ring3", `-O2`) |
| guest | `inbound_bench.c`, port 8080, `Connection: close`, backlog 8 (`ACCEPTQ_MAX`) |
| network | Firecracker `tap0`, static `ip=` boot param, guest 172.19.0.59 |
| driver | `drive.sh`, `N=1200`, `RECOVER_WAIT=180` |

## Result

```
phase 1 CHURN      attempted=1200  ok=1198  failed=2   first client fail at 816
phase 2 RECOVERY   ok=20 of 20
phase 3 KEEPALIVE  num_connects=1  HTTP 200   (500 requests, one connection)

guest, cumulative  served=1720  accept_ok=1720  accept_fail=5  recovered=1
```

**The ~350 figure does not reproduce.** The guest served 1200 consecutive
churned connections -- one connection per request, `Connection: close`, so every
request mints a TIME_WAIT PCB -- with **zero** `accept()` failures. It went on to
serve 1720 in total across all three phases with five transient accept failures
(`errno 11`, EAGAIN), every one of which recovered.

Nothing here behaves like a cap. The recovery phase returned 20 of 20 after a
180 s pause, and keep-alive moved 500 requests down a single connection
(`num_connects=1`). Whatever the original 350 referred to, it is not a property
of this build.

## The two client-side failures are the interesting part

The driver counted 1198 successes; the guest counted 1200 served. The design
anticipated exactly this: *a request the client believes failed but the server
handled is a different fault from one that never arrived.* Both misses were
client-side timeouts against a server that had already answered. Had the harness
trusted only the client count, it would have reported a 0.17% server error rate
that did not exist.

## A trap worth recording

The guest answered ICMP but refused every TCP handshake -- SYN retransmitted,
no SYN-ACK -- and **upstream's own `acceptq_close_test.c` failed identically**.
That looks exactly like a platform regression, and ring3 had landed hours
earlier, which made the story more plausible still.

It was the tap device. Locally-generated TCP is handed to a tap with
`CHECKSUM_PARTIAL` -- the checksum is not filled in, because on real hardware
the NIC would do it -- and lwIP drops those frames silently. Kernel-generated
ICMP carries a correct checksum, so ping worked and every TCP connection hung.

```sh
sudo ethtool -K tap0 tx off      # then it works immediately
```

Anyone measuring a userspace TCP stack behind a tap needs this, and the failure
signature (ICMP fine, TCP dead) points hard at the guest when the fault is on
the host.

Related: do **not** run `BareMetal-Firecracker/scripts/mkbr0.sh` on a remote
wired host. It enslaves the host NIC into `br0`, which drops the SSH session it
was invoked from. A standalone tap is enough -- `baremetal.sh` attaches `tap0`
directly, not the bridge.

## What this does not settle

`N=1200` was the configured ceiling, not a limit the platform reached; the true
ceiling is above 1200 and unmeasured. `SOCK_MAX` is 16 and `ACCEPTQ_MAX` is 8,
so *simultaneous* connections remain bounded and untested here -- this measures
cumulative lifetime requests on a single-threaded accept loop, which is what the
~350 claim was about.
