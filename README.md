# unikernel-c

Small C programs that run as [BareMetal](https://github.com/ReturnInfinity) unikernels — no operating system, no runtime, no interpreter — and still speak HTTPS to the outside world.

Two of them:

| | What it is |
|---|---|
| **`agent/`** | An LLM agent loop: the model is given tools, decides which to call, sees the results, and keeps going until it's done. Two builds — one ordinary, one with zero heap allocation. |
| **`prime-hunter/`** | A twin-prime search that reports records and progress to Telegram. Deployed and running on BareMetal Cloud in **16 MiB of RAM**. |

Both compile unchanged for Linux, macOS, and BareMetal.

## Why this exists

Most agent frameworks and most bots assume a language runtime, a garbage collector, and hundreds of megabytes of headroom. Neither of these does. The prime hunter completes TLS handshakes against `api.telegram.org` from inside a 2.8 MB unikernel image with sixteen megabytes of RAM — mbedTLS, lwIP, curl, a full CA bundle and the search itself, all inside that budget.

`docs/build-report.html` is a write-up of getting there: what broke, what the fixes were, and how long each phase took.

## The agent loop

The distinction worth caring about: a *pipeline* does the same thing every run. An *agent* chooses.

```
think   → the model answers, or asks for tools
act     → run what it asked for
observe → results go back into the conversation
repeat  → until it answers, or MAX_STEPS
```

The conversation array is the agent's entire memory, which is what lets step 4 depend on what step 2 saw. In a real run, the example agent rejected four random Wikipedia articles as uninteresting, invented its own search term, chased it two levels deep, and only then published — none of which a fixed pipeline can do.

Two details that break most hand-written tool loops in C, both handled and commented in the source:

- `function.arguments` arrives from the API as a **JSON-encoded string**, not an object. It needs a second parse.
- The assistant message must be appended back **verbatim, including its `tool_calls` array**, before the `role:"tool"` results. Omit that echo and every OpenAI-compatible API returns 400.

Provider is chosen at runtime — Gemini, Groq, or a local Ollama — because they all speak the same chat/completions shape. Nothing in the loop changes between them.

### Zero-allocation build

`agent/agent_static.c` performs no heap allocation of its own. Not by discipline — by construction:

```c
#define malloc   DO_NOT_malloc
#define free     DO_NOT_free
#define realloc  DO_NOT_realloc
#define strdup   DO_NOT_strdup
```

Anything added below that line fails to compile. cJSON allocates internally, so it's pointed at a static arena via `cJSON_InitHooks`; the arena is a bump allocator reset once per task. Total footprint is three fixed arrays, sized at compile time, and the program prints its own high-water mark so you can size them from evidence rather than guesswork. A measured 8-step run peaked at 200 KB.

## The prime hunter

Searches for twin primes — pairs like (11, 13) that sit two apart — and reports to Telegram when the gap between consecutive pairs beats the record, plus a heartbeat on a timer.

Every twin pair except (3, 5) has the form (6k−1, 6k+1), so the loop tests only those and skips five sixths of the number line. Primality is deterministic Miller–Rabin over a twelve-base witness set proven correct for all n < 2⁶⁴ — no probabilistic caveat.

Verified against an independent sieve, which is the only kind of verification worth reporting:

```
--- miller-rabin ---
below 1000000: 8168 twin pairs (excluding (3,5)), largest gap 1452
--- independent sieve (python) ---
below 1000000: 8168 twin pairs (excluding (3,5)), largest gap 1452
```

8169 including (3, 5) is the published count for 10⁶.

## Build

Local, either program:

```sh
make                              # builds everything into build/
make check                        # cross-checks the primality test against a sieve
./build/prime_hunter --seconds 60 # one minute, then stop
```

Manually, if you prefer:

```sh
gcc -O2 -o prime_hunter prime-hunter/prime_hunter.c -lcurl
gcc -O2 -o agent agent/agent.c -lcurl -lcjson
```

`libcjson` is only needed by the agent; the prime hunter has no dependency beyond libcurl.

### For BareMetal

```sh
git clone https://github.com/ReturnInfinity/BareMetal-App
cp prime-hunter/prime_hunter.c BareMetal-App/
cd BareMetal-App && ./setup.sh          # builds musl, lwIP, mbedTLS, curl
./1-build.sh prime_hunter.c             # produces baremetal.elf
./2-run.sh                              # local Firecracker microVM
```

BareMetal has no environment, so `getenv` returns nothing there and the `#define` defaults are used instead — bake your token and chat id in before building. `scripts/cloudup.sh` deploys the built image to BareMetal Cloud.

## Configuration

Environment variables where there are any; compile-time defaults where there aren't.

| Variable | Purpose |
|---|---|
| `TELEGRAM_BOT_TOKEN` | From [@BotFather](https://t.me/botfather) |
| `TELEGRAM_CHAT_ID` | Message the bot, then read it from `getUpdates` |
| `DISCORD_WEBHOOK` | Alternative destination for the agent |
| `GEMINI_API_KEY` / `GROQ_API_KEY` | Agent only; neither set means a local Ollama |
| `LLM_BASE_URL` / `LLM_MODEL` | Override the provider entirely |

## Notes from the port

Three things cost real time, recorded in case they save someone else's:

- **`mkbr0.sh` has two branches.** On a Wi-Fi host it configures NAT; on a wired host it enslaves the NIC to a bridge and moves the host IP across. On a single-NIC cloud box, that disconnects you mid-session. `scripts/netprep.sh` is a NAT-only version that never touches the host interface.
- **`baremetal.elf` carries no PVH ELF note**, so QEMU's `microvm` machine refuses it (`Error loading uncompressed kernel without PVH ELF Note`) even though the virtio-mmio layout otherwise matches Firecracker's. Testing without Firecracker means adding that note.
- **Baking config with `sed` breaks naive validation.** A startup check comparing the token against its own `#define` will match once the define *is* the token, and reject exactly the builds that are correctly configured. Validate by shape instead.

## Credits

The idea of a C program on BareMetal that calls an HTTP API and posts to a chat channel comes from [Flâneur the Wanderer](https://github.com/varunmadhok/Flaneur-the-wanderer) by varunmadhok (MIT). Flâneur is a fixed pipeline; the agent here is the tool-calling loop that pipeline doesn't have, and the prime hunter is a compute workload in the same shape.

BareMetal, BareMetal-App and BareMetal-AppPort are by [Return Infinity](https://github.com/ReturnInfinity).

## License

MIT — see [LICENSE](LICENSE).
