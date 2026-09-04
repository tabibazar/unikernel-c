# probe.py -- what can Python actually do on a machine with no operating system?
#
# BareMetal-AppPort ships a CPython port. This finds out how much of Python
# actually works on it, by running the same probes here and on ordinary Linux
# and comparing. A probe that fails on both is a probe bug; a probe that passes
# on Linux and fails on BareMetal is a finding.
#
# WHY NOT CPYTHON'S OWN TEST SUITE
#
# The port installs a *curated* stdlib onto its disk image, so the `test`
# package is not guaranteed to be there, and a harness that dies on
# `import test` measures nothing. These probes depend on nothing but the
# module under test. Where the real suite is available it is strictly better
# and should be run too -- this is the floor, not the ceiling.
#
# WHAT THE PORT ALREADY TELLS US TO EXPECT (OPENISSUES.md)
#
#   no fork/execve/wait4      -> subprocess, multiprocessing, os.system
#   cooperative threads, 32   -> thread stress, priority-inheritance mutexes
#   signals on a ~1ms tick    -> no SIGSEGV/SIGFPE handlers, no alarm/setitimer
#
# Those four are predictions, marked EXPECTED_FAIL below. A probe that fails
# for one of those reasons confirms the documentation. A probe that fails for
# any other reason is the interesting case, and the reason this is worth
# running rather than reasoning about.
#
# HANGING IS THE REAL RISK
#
# There is no way to time a probe out from inside: alarm() does not work here,
# and a probe that blocks forever takes the whole run with it and produces no
# output at all -- which reads identically to a crash. So anything that can
# block (sockets, joins, sleeps) runs LAST, after every safe probe has already
# printed its line. Output is flushed per probe for the same reason: on a
# platform with no files to collect afterwards, an unflushed buffer is a lost
# run.
#
#   BareMetal: deployed as /pylib/main.py, output read off the console
#   Linux:     python3 probe.py

import sys

PASS, FAIL, SKIP, XFAIL = "PASS", "FAIL", "SKIP", "XFAIL"
results = []


def probe(name, group, expect_fail=False):
    """Register one probe. The function returns a detail string, or raises."""
    def deco(fn):
        results.append((name, group, fn, expect_fail))
        return fn
    return deco


def run_all():
    print("PROBE_START python=%s platform=%s" % (
        sys.version.split()[0], sys.platform))
    print("PROBE_IMPL %s" % sys.implementation.name)
    counts = {PASS: 0, FAIL: 0, SKIP: 0, XFAIL: 0}
    for name, group, fn, expect_fail in results:
        try:
            detail = fn()
            status = PASS
            if expect_fail:
                # Marked because the port's docs predict this cannot work
                # there. On the Linux control it passes and the tag is just
                # a pointer; on BareMetal a pass here means the docs are
                # stale or the probe is too weak, and either is a finding.
                detail = "[predicted-fail-on-baremetal] " + str(detail)
        except NotImplementedError as e:
            status, detail = SKIP, str(e)
        except Exception as e:
            status = XFAIL if expect_fail else FAIL
            detail = "%s: %s" % (type(e).__name__, e)
        counts[status] += 1
        print("PROBE %-9s %-22s %-12s %s" % (status, group, name, detail))
        sys.stdout.flush()
    print("PROBE_TOTAL pass=%d fail=%d xfail=%d skip=%d" % (
        counts[PASS], counts[FAIL], counts[XFAIL], counts[SKIP]))
    print("PROBE_DONE")
    sys.stdout.flush()


# ----------------------------------------------------------- language core

@probe("closures_generators", "language")
def _():
    def outer(n):
        return lambda x: x + n
    g = (i * i for i in range(5))
    assert outer(3)(4) == 7 and sum(g) == 30
    return "ok"


@probe("comprehensions_slices", "language")
def _():
    d = {k: v for k, v in zip("abc", range(3))}
    assert [x for x in range(10)][2:5] == [2, 3, 4] and d["c"] == 2
    return "ok"


@probe("classes_properties", "language")
def _():
    class A:
        def __init__(self): self._x = 1
        @property
        def x(self): return self._x
        def __repr__(self): return "A()"
    assert A().x == 1 and repr(A()) == "A()"
    return "ok"


@probe("exceptions_traceback", "language")
def _():
    import traceback
    try:
        raise ValueError("boom")
    except ValueError:
        tb = traceback.format_exc()
    assert "ValueError: boom" in tb
    return "traceback formatting intact"


@probe("fstrings_walrus_match", "language")
def _():
    v = 7
    s = f"{v:03d}"
    if (n := v * 2) > 10:
        pass
    match s:
        case "007":
            r = "matched"
        case _:
            r = "no"
    assert r == "matched" and n == 14
    return "3.8/3.10 syntax ok"


@probe("recursion_limit", "language")
def _():
    def rec(n):
        return 0 if n == 0 else 1 + rec(n - 1)
    # Deliberately modest: the port's stack size is not discoverable from
    # inside, and blowing it here would take the whole run down rather than
    # failing this one probe.
    depth = 200
    assert rec(depth) == depth
    return "depth %d ok, limit reports %d" % (depth, sys.getrecursionlimit())


@probe("gc_refcount", "language")
def _():
    import gc
    class Node:
        pass
    a, b = Node(), Node()
    a.other, b.other = b, a           # a cycle: only the collector frees this
    del a, b
    n = gc.collect()
    return "gc.collect() reclaimed %d" % n


@probe("unicode_encodings", "language")
def _():
    s = "café ☃ \U0001F41C"
    assert s.encode("utf-8").decode("utf-8") == s
    assert "é".encode("latin-1") == b"\xe9"
    return "utf-8, latin-1 round-trip"


# ------------------------------------------------------------- data / stdlib

@probe("json", "stdlib-data")
def _():
    import json
    o = {"a": [1, 2.5, None, True], "b": "☃"}
    assert json.loads(json.dumps(o)) == o
    return "round-trip"


@probe("re", "stdlib-data")
def _():
    import re
    m = re.match(r"(\w+)-(\d+)", "abc-123")
    assert m.group(1) == "abc" and re.sub(r"\d", "#", "a1b2") == "a#b#"
    return "match, groups, sub"


@probe("collections_itertools_functools", "stdlib-data")
def _():
    import collections, itertools, functools
    c = collections.Counter("aabbbc")
    top = c.most_common(1)[0]
    pairs = list(itertools.combinations(range(4), 2))
    red = functools.reduce(lambda a, b: a + b, range(5))
    assert top == ("b", 3) and len(pairs) == 6 and red == 10
    return "Counter, combinations, reduce"


@probe("decimal_fractions_math", "stdlib-data")
def _():
    import decimal, fractions, math
    d = decimal.Decimal("0.1") * 3
    f = fractions.Fraction(1, 3) + fractions.Fraction(1, 6)
    assert str(d) == "0.3" and f == fractions.Fraction(1, 2)
    assert abs(math.sqrt(2) - 1.4142135623730951) < 1e-15
    return "exact decimal, rationals, libm-free sqrt"


@probe("datetime", "stdlib-data")
def _():
    import datetime
    d = datetime.datetime(2026, 9, 4, 12, 30)
    assert (d + datetime.timedelta(days=1)).day == 5
    return d.isoformat()


@probe("struct_base64_binascii", "stdlib-data")
def _():
    import struct, base64
    packed = struct.pack("<IHf", 1, 2, 3.5)
    assert struct.unpack("<IHf", packed)[2] == 3.5
    assert base64.b64decode(base64.b64encode(b"xyz")) == b"xyz"
    return "little-endian pack, b64"


@probe("hashlib_hmac", "stdlib-data")
def _():
    import hashlib, hmac
    h = hashlib.sha256(b"abc").hexdigest()
    assert h.startswith("ba7816bf")
    mac = hmac.new(b"k", b"m", hashlib.sha256).hexdigest()
    return "sha256 vector ok, hmac len %d" % len(mac)


@probe("textwrap_difflib_pprint", "stdlib-data")
def _():
    import textwrap, difflib
    w = textwrap.wrap("a " * 30, width=20)
    r = difflib.SequenceMatcher(None, "abcd", "abed").ratio()
    assert len(w) > 1 and 0 < r < 1
    return "wrap %d lines, ratio %.2f" % (len(w), r)


# ---------------------------------------------------------------- runtime

@probe("sys_platform_info", "runtime")
def _():
    import platform
    return "machine=%s system=%s maxsize=%d" % (
        platform.machine() or "?", platform.system() or "?", sys.maxsize)


@probe("time_clocks", "runtime")
def _():
    import time
    t0 = time.perf_counter()
    for _ in range(20000):
        pass
    dt = time.perf_counter() - t0
    # A clock that never advances is this platform's known failure mode --
    # docs/aco-r1/ hit exactly that with C's clock(). Assert it moves.
    assert dt > 0, "perf_counter did not advance"
    return "perf_counter advanced %.6fs; time()=%.0f" % (dt, time.time())


@probe("random_urandom", "runtime")
def _():
    import random, os
    random.seed(1234)
    a = [random.random() for _ in range(3)]
    random.seed(1234)
    assert a == [random.random() for _ in range(3)], "seeding not reproducible"
    ent = os.urandom(16)
    assert len(ent) == 16 and ent != b"\x00" * 16
    return "seeded repeatable; os.urandom returns entropy"


@probe("memory_pressure", "runtime")
def _():
    # The heap here is a bump allocator over whatever RAM the VM was given.
    # Ten megabytes is enough to notice a 16 MiB machine without killing it.
    chunks = [bytearray(1024 * 512) for _ in range(20)]
    total = sum(len(c) for c in chunks)
    del chunks
    return "allocated and freed %d MiB" % (total // (1024 * 1024))


@probe("sqlite3", "runtime")
def _():
    import sqlite3
    con = sqlite3.connect(":memory:")
    con.execute("create table t(a int, b text)")
    con.executemany("insert into t values(?,?)", [(1, "x"), (2, "y")])
    rows = con.execute("select b from t where a>?", (1,)).fetchall()
    con.close()
    assert rows == [("y",)]
    return "in-memory database ok"


@probe("importlib_dynamic", "runtime")
def _():
    import importlib
    m = importlib.import_module("base64")
    return "imported %s dynamically" % m.__name__


@probe("ctypes_dlopen", "runtime")
def _():
    # Decides whether C extensions -- and therefore numpy, cryptography,
    # anything with a compiled wheel -- are possible at all on this platform.
    import ctypes
    lib = ctypes.CDLL(None)
    return "ctypes loaded; CDLL(None) -> %r" % (lib,)


# ------------------------------------------------------------------- files

@probe("file_io", "filesystem")
def _():
    import os
    p = "/tmp/probe_test.txt" if os.path.isdir("/tmp") else "probe_test.txt"
    with open(p, "w") as f:
        f.write("hello\nworld\n")
    with open(p) as f:
        lines = f.readlines()
    os.remove(p)
    assert lines == ["hello\n", "world\n"]
    return "write, read, remove at %s" % p


@probe("os_stat_listdir", "filesystem")
def _():
    import os
    entries = os.listdir(".")
    st = os.stat(".")
    return "%d entries in cwd, mode=%o" % (len(entries), st.st_mode & 0o777)


@probe("pathlib", "filesystem")
def _():
    import pathlib
    p = pathlib.Path(".") / "x" / "y.txt"
    assert p.name == "y.txt" and p.suffix == ".txt"
    return "pure path manipulation ok"


@probe("tempfile", "filesystem")
def _():
    import tempfile
    with tempfile.NamedTemporaryFile(mode="w+", delete=True) as f:
        f.write("data")
        f.flush()
        f.seek(0)
        assert f.read() == "data"
    return "NamedTemporaryFile round-trip"


# ------------------------------------------------------------ concurrency

@probe("threading_basic", "concurrency")
def _():
    import threading
    out = []
    lock = threading.Lock()

    def worker(i):
        with lock:
            out.append(i)

    ts = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert sorted(out) == [0, 1, 2, 3]
    return "4 threads, lock, join"


@probe("threading_queue", "concurrency")
def _():
    import threading, queue
    q = queue.Queue()

    def producer():
        for i in range(10):
            q.put(i)
        q.put(None)

    t = threading.Thread(target=producer)
    t.start()
    got = []
    while True:
        v = q.get()
        if v is None:
            break
        got.append(v)
    t.join()
    assert got == list(range(10))
    return "producer/consumer over Queue"


@probe("thread_pool", "concurrency")
def _():
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=4) as ex:
        vals = list(ex.map(lambda x: x * x, range(8)))
    assert vals == [x * x for x in range(8)]
    return "ThreadPoolExecutor.map"


@probe("asyncio", "concurrency")
def _():
    import asyncio

    async def main():
        await asyncio.sleep(0)
        return 42

    return "event loop returned %d" % asyncio.run(main())


# ------------------------------------------ documented to fail on this port

@probe("subprocess", "process-model", expect_fail=True)
def _():
    import subprocess
    out = subprocess.run([sys.executable, "-c", "print(1)"],
                         capture_output=True, timeout=5)
    return "returned %r" % out.stdout


@probe("os_fork", "process-model", expect_fail=True)
def _():
    import os
    if not hasattr(os, "fork"):
        raise AttributeError("os.fork absent")
    pid = os.fork()
    if pid == 0:
        os._exit(0)
    os.waitpid(pid, 0)
    return "forked and reaped pid %d" % pid


@probe("multiprocessing", "process-model", expect_fail=True)
def _():
    import multiprocessing
    with multiprocessing.Pool(2) as p:
        vals = p.map(abs, [-1, -2])
    return "pool returned %r" % vals


@probe("signal_alarm", "process-model", expect_fail=True)
def _():
    import signal
    if not hasattr(signal, "alarm"):
        raise AttributeError("signal.alarm absent")
    fired = []
    signal.signal(signal.SIGALRM, lambda s, f: fired.append(1))
    signal.alarm(1)
    signal.pause()
    signal.alarm(0)
    return "alarm delivered"


@probe("signal_handler_raise", "process-model")
def _():
    # Software-raised signals ARE documented to work, so this one is a real
    # expectation rather than a predicted failure. It separates "no signals"
    # from "no ASYNCHRONOUS signals".
    import signal
    got = []
    prev = signal.signal(signal.SIGUSR1, lambda s, f: got.append(s))
    signal.raise_signal(signal.SIGUSR1)
    signal.signal(signal.SIGUSR1, prev)
    assert got == [signal.SIGUSR1]
    return "raise_signal delivered to handler"


# -------------------------------------------------- last: things that block

@probe("socket_create", "network")
def _():
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.close()
    return "AF_INET/SOCK_STREAM created and closed"


@probe("ssl_module", "network")
def _():
    import ssl
    ctx = ssl.create_default_context()
    return "OpenSSL/mbedTLS context: %s" % ssl.OPENSSL_VERSION


@probe("dns_and_http", "network")
def _():
    # Genuinely blocking, and last on purpose: if the network is not up this
    # is where the run stops, and by then everything else has already printed.
    import socket
    socket.setdefaulttimeout(8)
    addr = socket.gethostbyname("example.com")
    s = socket.create_connection((addr, 80), timeout=8)
    s.sendall(b"HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n")
    head = s.recv(64)
    s.close()
    return "%s -> %r" % (addr, head[:16])


if __name__ == "__main__":
    run_all()
