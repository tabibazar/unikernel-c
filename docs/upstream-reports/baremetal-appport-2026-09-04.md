# Three issues in BareMetal-App / BareMetal-AppPort

Found while building the CPython port to measure what fraction of Python works
on the platform. All three are reproducible from a clean clone; none is
speculative. Ordered by how much time each is likely to cost someone.

**Environment.** Fresh `git clone` of `ReturnInfinity/BareMetal-App` on
2026-09-04, built on Ubuntu 24.04 x86-64 and separately in a Debian bookworm
container. Firecracker 1.7.0.

---

## 1. No fetch script validates its download (9 scripts)

**Severity: this is the one that wasted our time.** It cost us a build locally
and contributed to a lost session on a rented bare-metal instance.

Every `scripts/get-*.sh` fetches with the same pattern:

```sh
curl -s -L -o "${TARBALL}" "${URL}"
```

No `--fail`, no checksum, no content check. `curl` without `--fail` writes the
HTTP error body to the output file and exits 0, so **any upstream 4xx or 5xx is
saved as the artifact and the script proceeds happily.**

Confirmed across all nine: `get-musl.sh`, `get-lwip.sh`, `get-mbedtls.sh`,
`get-cacert.sh`, `get-curl.sh`, `get-sqlite.sh`, `get-libsodium.sh`,
`get-lwext4.sh`, `get-python.sh`. None performs a checksum; none passes `-f`.

### What we actually hit

`download.savannah.nongnu.org` returned **HTTP 502** during a build.
`get-lwip.sh` saved the 166-byte error page as `build/lwip-2.2.0.zip`:

```
$ head -c 80 build/lwip-2.2.0.zip
<html>
<head><title>502 Bad Gateway</title></head>
```

`setup.sh` then ran for several more minutes before failing here:

```
  End-of-central-directory signature not found.  Either this file is not
  a zipfile, or it constitutes one disk of a multi-part archive. ...
unzip:  cannot find zipfile directory in one of lwip-2.2.0.zip or
        lwip-2.2.0.zip.zip, and cannot find lwip-2.2.0.zip.ZIP, period.
```

Nothing in that message mentions the network, names the URL, or suggests
deleting the file — and because `get-lwip.sh` skips the download when the file
already exists, **re-running `setup.sh` fails identically forever** until
someone works out to remove it by hand. The failure is also far from its cause
in both time and text.

### Suggested fix

Two lines per script. `--fail` makes curl exit non-zero on an HTTP error, and
removing the partial output stops the poisoned-cache behaviour:

```sh
curl -sSfL -o "${TARBALL}" "${URL}" || { rm -f "${TARBALL}"; exit 1; }
```

A pinned checksum would be stronger still, and these are already
version-pinned, so the digest is a constant:

```sh
echo "${SHA256}  ${TARBALL}" | sha256sum -c - || { rm -f "${TARBALL}"; exit 1; }
```

Worth having on a build that runs unattended: a corrupted dependency should
fail at the point of download, naming the URL.

---

## 2. `PYTHON.md` documents 3.12.8; everything builds 3.14.7

The port builds **CPython 3.14.7**:

| location | version |
|---|---|
| `scripts/get-python.sh:15` | `VERSION="3.14.7"` |
| `setup.sh:56` | `PYTHON_DIR="$BUILD_DIR/Python-3.14.7"` |
| `build-app.sh:57` | `PYTHON_DIR="$BUILD_DIR/Python-3.14.7"` |
| `port/python_port/install-stdlib.sh:34` | `LIB=".../Python-3.14.7/Lib"` |

`PYTHON.md` says 3.12.8, in four places — lines 3, 8, 57 and 132.

This is more than a stale version string. Line 57 opens a section headed:

> "What's confirmed, from reading CPython **3.12.8**'s actual source (not …"

So the document's analysis — which macros matter, what the bootstrap module set
contains, what `Modules/Setup.bootstrap.in` looks like — was validated against a
CPython the tree no longer builds. `get-python.sh`'s own comment says the port
is "written against this exact release's `Modules/Setup.bootstrap.in` layout,
pyconfig.h.in macro list, and `Python/thread_pthread.h` contents", which makes
the version genuinely load-bearing rather than incidental.

Either the doc needs updating to 3.14.7, or — if the analysis really was only
ever done against 3.12.8 — that is worth saying explicitly, because a reader
currently cannot tell which release the reasoning applies to.

---

## 3. `baremetal.sh` still cannot start Firecracker (previously reported)

This was reported from our side earlier and is **still present in a clone taken
today**, so flagging it again rather than assuming it is in flight.

`baremetal.sh` removes the Firecracker log file and then passes that same path
as `--log-path`:

```
46:		rm -f "$SOCKET"
47:		rm -f "$FCLOG"
...
71:			firecracker --api-sock "$SOCKET" --log-path "$FCLOG"
```

Firecracker opens `--log-path` but does not create it, so it exits immediately
and the VM never boots:

```
Could not initialize logger: Failed to open target file: No such file or
directory (os error 2)
Error: LoggerInitialization(LoggerUpdateError(Os { code: 2, kind: NotFound,
message: "No such file or directory" }))
Firecracker exiting with error. exit_code=1
```

One line after the `rm` fixes it, confirmed working on our host:

```sh
rm -f "$FCLOG"
touch "$FCLOG"
```

---

## Why we were in here

We are measuring what fraction of Python actually works on the platform — a
capability inventory across the language core, stdlib, filesystem, concurrency,
process model and network, run on BareMetal and on Linux from the same source
and diffed. Short-lived Python sandboxes are the workload the CPython port is
best placed to serve, and no published number exists for it.

Two things we noticed that are **not** bugs, but which we would have liked
stated near the top of `PYTHON.md`, since both change how the port is pitched:

- **`python.app` is 9,621,136 bytes**, and `baremetal.sh` sets `MEMSIZE=32`
  with the comment that Python needed at least 28 MiB. The 16 MiB figure that
  appears in a lot of BareMetal material does not hold for a Python guest.
- **The stdlib is curated** — roughly forty modules deployed by
  `install-stdlib.sh`. That is a sensible choice, but "CPython runs here" and
  "the Python standard library is available here" are different claims, and
  users will assume the second.

Happy to send raw logs, the exact 502 capture, or the probe harness.
