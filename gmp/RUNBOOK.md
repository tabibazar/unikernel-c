# Chain hunt: how to restart it

Everything here is resumable. The state that matters is on disk, not in any
process or session.

## What is running

| | where | state |
|---|---|---|
| local hunt | this Mac, one core | `gmp/results/hunt-local/next_m` |
| cloud workers | BareMetal Cloud, up to 4 | slice baked into each image |

The local hunt is a plain `nohup`'d shell loop. It survives this Claude session
ending; it does not survive a reboot. Restarting it costs nothing:

```sh
cd <repo>
nohup ./gmp/scripts/hunt-local.sh > /tmp/hunt-local.out 2>&1 &
./gmp/scripts/hunt-local.sh --status
```

It resumes from `next_m`, so the worst case is repeating the chunk that was in
flight. Nothing is lost and nothing is double-counted except that one chunk.

## The two stages

Sieve on a real host, PRP on the workers. This split is not incidental:
sieving to depth 1e9 enumerates ~5.8e7 primes at a cost that is *fixed*, not
per-multiplier (200k multipliers 38.0 s, 2M multipliers 39.2 s), so it is run
once per range and the survivors — a few thousand 64-bit integers — are shipped
to the workers.

## Deploying a cloud worker

BareMetal takes no argv, so a worker's range is compiled in. One slice, one
image, one instance.

```sh
# 1. sieve a range and build the unikernel (free; Docker, amd64 emulated)
./gmp/scripts/build-worker-docker.sh w0 1000000000 2000000

# 2. upload and start it
cd ~/BareMetal-App
export BM_API_KEY=$(cat ~/.bm_api_key)
IMG=$(./bm-api.sh images upload cc-w0 <repo>/gmp/results/workers/w0/baremetal.elf | awk -F': ' '/^id:/{print $2}')
INST=$(./bm-api.sh instances create cc-w0 1 16 "$IMG" | awk -F': ' '/^id:/{print $2}')
./bm-api.sh instances start "$INST"

# 3. collect — the console is the only record
./bm-api.sh instances logs "$INST"
```

**Ranges must not overlap.** Nothing enforces this; the caller allocates them.
The local hunt owns `m` from 1 upward, so cloud slices start at 1e9 to stay
clear of it. Record every slice you allocate.

Building needs Linux x86-64 and a toolchain but **not KVM** — only *running*
Firecracker needs that. That is why the build works in an emulated container on
a Mac and does not need a rented bare-metal host. This was got wrong once here,
at the cost of a c5.metal session.

## Costs and caps

- BareMetal Cloud: $0.00501056 per instance-hour, `maxInstancesPerUser` 4,
  `maxRamMibPerInstance` 16, `maxVcpuPerInstance` 1.
- $23 of credit at 4 instances is about 1,148 hours, or 48 days.
- **Do not use c5 on AWS.** If AWS is ever needed, spot only, never on-demand:
  c5.metal spot was $0.47/hr against $4.08 on-demand for identical hardware.

## Reading a result

`WORKER_HIT` / `HUNT_HIT` lines are **candidates, not records**.
`mpz_probab_prime_p` is Miller-Rabin plus Baillie-PSW, so a "prime" is a
probable prime. A full-length chain must be re-tested and then *proven* before
it is claimed anywhere. The worker's only job is to not lose it.

## If results look wrong

Both stages carry known-answer selftests and the driver refuses to run if
either fails — a hunt that sieves wrongly is indistinguishable from a hunt that
is merely unlucky, for as long as it runs.

```sh
gmp/results/hunt-local/cc_sieve --selftest   # brute-force, both directions
gmp/results/hunt-local/cc_hunt  --selftest   # classical chains, checkable by eye
```

Expected shape at ~1000 digits, primorial 2357#, depth 1e9: survival about
2.6e-3, and a chain-length histogram falling roughly 60-70x per term. A drift in
either means something is wrong, not that the search got unlucky.

## The cloud tender

`gmp/scripts/cloud-tender.sh` keeps the workers fed. A slice is baked into its
image, so a worker finishes in about two hours and stops; without something
watching, three instances would work for two hours and idle for the rest of a
three-day run.

It polls every five minutes, and for any stopped `cc-w*` instance it harvests
the console, writes it to `results/hunt-local/cloud/logs/`, deletes the
instance, builds the next slice, and redeploys.

Because it spends money unattended, it is deliberately timid:

- it only ever touches instances named `cc-w*` — `bmagent` and anything a human
  created are invisible to it
- `MAX_DEPLOYS` (default 60) caps redeploys for the whole run; when reached it
  exits rather than continuing quietly
- `touch results/hunt-local/cloud/STOP` halts it at the next poll
- the console log is written to disk *before* the instance that produced it is
  deleted, because the console is the only copy of that work

```sh
./gmp/scripts/cloud-tender.sh --status     # ranges, budget used, hits
touch gmp/results/hunt-local/cloud/STOP    # stop it
```

At three instances the burn is $0.015/hr, about $1.08 for three days against
$23 of credit. `MAX_DEPLOYS` at 60 bounds the worst case to roughly 120
instance-hours, or about $0.60 of compute, even if something loops.
