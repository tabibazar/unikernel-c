# Draft: capacity request to Return Infinity

**Ask in one line:** raise `maxInstancesPerUser` from 4 to ~100 for one account,
for a bounded run, to attempt a public number-theory record on BareMetal Cloud.

---

Subject: Raising our instance cap for a record attempt on BareMetal Cloud

Hi,

We have been building on BareMetal and BareMetal Cloud for a few months, and we
would like to ask for more instances for one specific job.

**What we want to run.** A search for a Cunningham chain of length 6 -- a
size record in a well-known open table. It is pure integer arithmetic, so it
suits a unikernel well: no filesystem, no inbound network, no shared state
between workers. Each worker takes a residue class, tests candidates, and
reports anything it finds over outbound HTTPS. If a worker dies we lose one
work unit and nothing else.

**Why we are asking you and not renting a big server.** We measured both. Your
rate is $0.00501056 per instance-hour; c5.metal spot on AWS works out at
$0.004916 per core-hour. They are the same price to within 2%. So this is not
us looking for cheap compute -- we would rather run it on your platform, on
Firecracker, using the stack we have already ported to.

**What is blocking us.** `GET /api/limits` returns:

    {"maxVcpuPerInstance":1,"maxRamMibPerInstance":16,"maxInstancesPerUser":4}

Four cores. The job needs 21,326 core-hours, which we measured rather than
estimated (10.517 ms per primality test, 1014-digit operands, on a c5.metal
against a Linux control on the same machine). At four cores that is 222 days.
At around a hundred it is nine days. The cost is about $107 either way -- the
cap changes the calendar, not the bill.

The 16 MiB per-instance limit is fine for this workload. Our test binary links
to 350 KB and the numbers themselves are kilobytes. We are not asking you to
change that.

**What we would ask for specifically:** `maxInstancesPerUser` raised to ~100 on
one account, for a bounded window of a few weeks. Happy to accept lower
priority, preemption, off-peak scheduling, or a hard spend cap -- the work is
interruptible by design.

**What you would get.** If it lands, a public record found on BareMetal Cloud,
which we would say plainly and publicly. Either way you get the engineering we
have been doing on the way there. So far that includes:

- Three issues reported from a clean clone: every `scripts/get-*.sh` uses
  `curl` without `--fail`, so an upstream 502 is saved as the artifact and the
  build fails minutes later with an unrelated unzip error; `PYTHON.md`
  documents 3.12.8 while the tree builds 3.14.7; and `baremetal.sh` still
  removes its Firecracker log path and then passes it as `--log-path`, so the
  VM does not boot.
- A measured capability inventory of the CPython port: 40 probes run on
  BareMetal and on Linux from the same source and diffed. 19 of 20 gaps are
  missing stdlib modules rather than broken ones.
- A measurement retiring an internal assumption of ours that inbound serving
  fails at ~350 requests. It does not: 1200 churned connections, zero accept()
  failures, full recovery after a pause.

One thing worth flagging from the last of those, since it will bite someone
else: a guest behind a Linux tap answers ICMP but silently drops every inbound
TCP segment until `ethtool -K tap0 tx off` is set, because locally generated
TCP arrives with `CHECKSUM_PARTIAL` and lwIP discards it. It looks exactly like
a stack bug in the guest. Might be worth a line in the networking docs.

Happy to jump on a call, or to start with a smaller cap -- even 16 instances
would let us validate the harness end to end at full width before asking for
the rest.

Thanks,
Reza

---

## Notes for us, not for them

- Sources: `gmp/results/prp-ring3-2026-09-04.txt` (10.517 ms, parity with
  Linux), `netbench/results/inbound-ring3-2026-09-04.md` (1200 requests),
  `pyprobe/results/RESULTS.md` (40 probes), `docs/upstream-reports/`.
- The fallback if they say no is AWS **spot**, not on-demand: c5.metal spot is
  $0.47-0.60/hr against $4.08 on-demand, and the workload is interruptible.
  Never on-demand.
- Do not oversell the record. A k=6 find is probabilistic; the honest framing
  is a ~95% chance at 3x expectation, not a certainty.
