# What fraction of Python actually works on BareMetal

40 probes, run on BareMetal and on Linux **on the same machine, from the same
CPython 3.14.7 source tree**, and diffed. The comparison is the whole design: a
probe that fails on both is a probe bug; one that passes on Linux and fails on
BareMetal is a finding about the port.

| | |
|---|---|
| host | AWS c5.metal, Xeon Platinum 8275CL @ 3.00GHz |
| build | BareMetal-App `82ad9a7`, BareMetal-AppPort `292e4ae` ("ring3", `-O2`) |
| guest | `python.app`, 7,137,552 bytes, `MEMSIZE=32` |
| control | CPython 3.14.7 built from the tree's own tarball |

## Result

```
BareMetal   pass=19  fail=1  xfail=1  absent=19  skip=0
```

**Passing on Linux but not on BareMetal: 20 of 40 — and 19 of those never ran
at all.**

That distinction is the finding. Nineteen probes did not fail; they could not
start, because the module they import is not deployed. Only **one** probe
reached the interpreter and behaved differently, and one more failed exactly as
the port's documentation predicts.

| outcome | n | meaning |
|---|---|---|
| PASS | 19 | works |
| ABSENT | 19 | module not in the curated stdlib — never executed |
| XFAIL | 1 | `os.fork` absent, as documented |
| FAIL | 1 | `dns_and_http` — see caveat below |

## The constraint is the stdlib, not the interpreter

Every ABSENT is a missing module, not a broken one:

```
asyncio  concurrent  ctypes  decimal  hashlib  heapq  importlib
multiprocessing  pathlib  queue  signal  sqlite3  ssl  subprocess
tempfile  textwrap  traceback
```

The language core is in good shape. What is missing is breadth of library, and
several of these are load-bearing for ordinary code: `pathlib`, `hashlib`,
`traceback`, `tempfile`, `decimal`, `queue` and `concurrent` are not exotic.
`sqlite3` and `ssl` are absent from the *Python* side even though the port
vendors SQLite and mbedTLS for C.

This sharpens the point already sent upstream: **"CPython runs here" and "the
Python standard library is available here" are different claims.** The
interpreter is real, and 19 of 20 gaps close by deploying more of a stdlib that
already exists rather than by fixing anything.

`os.fork` is the one genuine structural limit, and it is documented: no fork
means no `subprocess` and no `multiprocessing` regardless of what gets deployed.

## Cold start

```
COLD_START_TO_FIRST_PYTHON_LINE 1.007 s
```

Worth stating plainly next to the alternative measured earlier in this repo:
AWS Lambda cold start came in at **44.8 ms** (`bench/lambda/RESULTS.md`). For
short-lived Python sandboxes — the workload the port is best placed to serve —
a one-second cold start is not currently competitive on that axis.

## One FAIL is mine, not the platform's

`dns_and_http` failed with `gaierror: Name does not resolve`. The guest ran on
an isolated host tap with no NAT and no DNS server, and its console shows it
falling back to 8.8.8.8, which was unreachable by construction. **This is an
artifact of the test network, not a port defect**, and it should be re-run on a
guest with real egress before anyone draws a conclusion from it. Counting it as
a BareMetal failure would be wrong.
