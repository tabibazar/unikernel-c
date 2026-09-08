# Testing the upstream argv parser without a metal host

`crt0.c`'s `fc_parse_args_param()` is the new code that turns Firecracker's
kernel cmdline into argv. Booting a guest to exercise it needs `/dev/kvm`, which
a Mac does not have — but the parser is a pure function on a string, so the part
that can actually be wrong is testable here for nothing.

`test_argv.c` **extracts the function verbatim** from the upstream `crt0.c` in
the build volume, rather than retyping it, and redirects only
`FC_ARGS_PARAM_ADDR` at a test buffer. A divergence between what was read and
what ships cannot hide in a copy.

```sh
docker run --rm --platform linux/amd64 -v bmport-build:/work bmworker-build:latest \
  cat /work/BareMetal-App/BareMetal-AppPort/port/crt0.c > crt0-upstream.c
# extract fc_parse_args_param into argv_parser.inc, then:
gcc -O2 -o test_argv test_argv.c && ./test_argv
```

## Result, 2026-09-08 against AppPort 88625c2

All 13 cases pass. The ones that earn their place:

| cmdline | argv | why it matters |
|---|---|---|
| `` args=`Test1 Test2` `` | main,Test1,Test2 | the documented case |
| `` ip=1.2.3.4 args=`a b` root=… `` | main,a,b | token found mid-line, not assumed to be a prefix |
| `` myargs=`x y` `` | main | **whole-token check works** — a prefix match would wrongly fire |
| `` args=`unterminated `` | main | no closing backtick degrades safely |
| `` args=`` `` | main | empty quotes are not an empty argument |
| `args=notquoted` | main | missing backtick degrades safely |
| 40 arguments | argc=32 | capped at `FC_ARGS_MAX_ARGC`, no write past the caller's array |
| 400 bytes, no NUL, no token | main | copy is bounded, does not run off the reserved region |

## What this does NOT test

**The boot path.** That Firecracker actually writes the cmdline to `0x5a00`,
that `baremetal.sh` quotes `` args=`$*` `` correctly through the shell and the
JSON body, and that musl's startup receives the fabricated argv. All of that
needs a running guest, therefore KVM, therefore a metal host we are not renting.

So: the parser is tested, the plumbing is not. Ian reports the plumbing works
locally, and nothing here contradicts that — it simply has not been confirmed
from this side.
