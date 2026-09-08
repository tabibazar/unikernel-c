# argv on real hardware, and what finding 3 actually does

Tested on a rented **m5zn.metal spot in us-east-2 at $0.611/hr**, 18 minutes,
**$0.18**. Not a c5, spot not on-demand. Fresh clone of BareMetal-App at
`692af98`, stock `setup.sh`, Firecracker installed from upstream releases.

## argv works end to end

`argvtest.c` prints exactly what `main()` receives, so this is the boot path
rather than the parser in isolation.

| command | result |
|---|---|
| `./baremetal.sh start Test1 Test2` | argc=3 — `main`, `Test1`, `Test2` |
| `./baremetal.sh start` | argc=1 — `main` |
| `./baremetal.sh start 103780000000 105000000` | argc=3 — `main`, `103780000000`, `105000000` |
| `./baremetal.sh start one two three four` | argc=5 — `main`, `one`, `two`, `three`, `four` |

The third row is the one we care about: a worker can be handed its range at boot
instead of having it compiled in.

## Finding 3 is version-dependent, and the failure is a silent hang

Same command, same tree, three configurations:

| Firecracker | `baremetal.sh` | result |
|---|---|---|
| 1.7.0 | stock | **fails** |
| 1.16.1 | stock | boots, argc=3 |
| 1.7.0 | + `touch "$FCLOG"` | boots, argc=3 |

Ian was right that 1.15+ resolves it. But the failure mode on an older
Firecracker is worse than the original report described. It does not error —
`baremetal.sh` spins forever in

```sh
while [ ! -S "$SOCKET" ]; do sleep 0.05; done
```

because Firecracker exited before creating the socket. The run had to be killed
after 25 seconds. The real message sits in the screen log, where nobody looks:

```
Could not initialize logger: Failed to open target file: No such file or directory (os error 2)
Firecracker exiting with error. exit_code=1
```

The third row is the useful one: one `touch` after the `rm` makes it version
independent, and turns a silent hang into a working VM.

## What this changes for us

Nothing immediately — argv is not on BareMetal Cloud yet, so `cc_worker` keeps
its baked slice. But the boot path is now confirmed rather than assumed, so when
the cloud gains argv the switch is a known quantity rather than a hope.
