# unikernel-c

Small C programs that run as [BareMetal](https://github.com/ReturnInfinity) unikernels — no operating system, no runtime, no interpreter — and still speak HTTPS to the outside world.

Five of them:

| | What it is |
|---|---|
| **`agent/`** | An LLM agent loop: the model is given tools, decides which to call, sees the results, and keeps going until it's done. Two builds — one ordinary, one with zero heap allocation. |
| **`prime-hunter/`** | A twin-prime search that reports records and progress to Telegram. Runs on BareMetal Cloud in **16 MiB of RAM**. |
| **`cunningham/`** | A swarm worker hunting Cunningham chains, coordinating with its peers through arithmetic rather than a coordinator. |
| **`supervisor/`** | The agent that watches the swarm: diagnoses faults from evidence and heals them, with the guardrails in code rather than in the prompt. |
| **`research/`** | A stateless research agent: ask a question, it searches and reads until it can answer, then exits carrying nothing forward. |

They all compile unchanged for Linux, macOS, and BareMetal.

## Why this exists

Most agent frameworks and most bots assume a language runtime, a garbage collector, and hundreds of megabytes of headroom. Neither of these does. The prime hunter completes TLS handshakes against `api.telegram.org` from inside a 2.8 MB unikernel image with sixteen megabytes of RAM — mbedTLS, lwIP, curl, a full CA bundle and the search itself, all inside that budget.

`docs/unikernels-explained.html` is the place to start if the idea is new: what exokernels and unikernels are, what a unikernel removes that a container does not, and what all of this measured. `docs/technical-report.html` is the narrower write-up: what was measured and how, including a controlled throughput comparison, the search results, and a reproduced finding of nondeterministic 64-bit arithmetic in the BareMetal runtime. `docs/swarm-run-01.md` and `docs/swarm-run-02.md` carry the raw results of the two swarm deployments.

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

## The swarm, and the agent that watches it

Three workers hunt Cunningham chains — p, 2p+1, 4p+3, … every term prime. Chain length is steeply graded, which is what makes it worth distributing: below 87 million a single worker finds 3659 chains of length 4, 140 of length 6, 18 of length 7 and exactly one of length 9 (at 85,864,769, the smallest of that length — which is how the implementation was checked).

Coordination has no coordinator. Worker *i* takes the candidates whose index is congruent to *i* modulo the swarm size. Coverage is complete and disjoint by arithmetic; nobody talks to anybody; a worker dying loses its residue class and nothing else.

**The workers are deliberately dumb, and that's correct** — nothing about testing primes needs a model. What needs one is the question *"is worker 1 broken, and if so, what broke?"*, because the answer isn't enumerable in advance. It crashed. The instance was reclaimed. DNS failed. It hit the instance cap. Or it's perfectly healthy and its slice of the number line simply had nothing to announce — in which case silence is correct and intervening is the bug.

### Guardrails belong in the code

The first version put the rules in the system prompt: *health is not measured by how recently a worker found something.* On its first run against a completely healthy swarm, the supervisor restarted a worker anyway, reasoning that it "only reported 'best 1'... indicating no chains were found." The reboot left that instance STOPPED, so the intervention actively degraded the swarm it was meant to protect.

The fix wasn't a better prompt. It was moving the decision out of the model's reach:

- **Health is computed in C** — instance RUNNING, network up, no error lines in the log. Find counts are excluded by construction, and every tool result carries the computed verdict.
- **`restart_worker` refuses** a worker that looks healthy, and refusal costs no budget. The model cannot argue past it.
- **One corrective action per run**, counted in a C variable.
- **The tool owns the end state**: `reboot` on this API leaves an instance STOPPED, so restart re-checks and issues a `start` if needed, then reports the status it actually observed.
- **The operator always hears.** If the model finishes without calling `send_report`, the loop sends its conclusion anyway. A supervisor whose reporting depends on the model's diligence isn't one.

Blast radius is structural too: tools accept a worker *name*, only names matching `cunningham-w<digit>` are allowed, and the instance id is resolved internally from the API's own output. The model never supplies a shell string, a path, or an id — so the program cannot touch a non-swarm instance whatever it is asked to do.

### Verified both ways

Fault injection is the only honest test of a healing agent — one direction proves it acts, the other proves it doesn't act when it shouldn't.

| Scenario | Expected | Result |
|---|---|---|
| Healthy swarm | Report all-clear, **0** actions | Checked all three, reported healthy, 0 actions |
| `instances stop cunningham-w2` | Detect, restart, verify, report | Detected STOPPED, restarted, re-listed to confirm RUNNING, reported to Telegram, 1 action |

One operational note: `gemini-flash-latest` queues indefinitely under load rather than returning an error, while the pinned `gemini-2.5-flash` either answers or 503s honestly. The supervisor retries with exponential backoff on 429/5xx and timeouts, because hosted models fail transiently and a supervisor less reliable than the thing it supervises is worse than none.

```sh
make
BM_API_DIR=~/BareMetal-App LLM_MODEL=gemini-2.5-flash \
GEMINI_API_KEY=... TELEGRAM_BOT_TOKEN=... TELEGRAM_CHAT_ID=... ./build/supervisor
```

One diagnosis cycle per invocation, then exit — cheap to schedule, easy to reason about.

## The research agent

Ask a question; it searches, decides what is still missing, reads the pages worth reading, answers, and exits. Nothing persists between questions — no history, no cache — so every run starts from nothing.

```sh
export GEMINI_API_KEY=... FIRECRAWL_API_KEY=...
./build/research "which version of mbedTLS does BareMetal-AppPort port, and does it include cJSON?"
```

```
-> web_search  {"query":"ReturnInfinity BareMetal-AppPort mbedTLS cJSON"}
-> read_page   {"url":"https://github.com/ReturnInfinity/BareMetal-AppPort"}
-> read_page   {"url":".../blob/main/README.md"}
-> submit_answer

Ports Mbed TLS 3.6.6. Does not include cJSON; the README lists musl libc,
lwext4, lwIP, curl, SQLite and CPython, but not cJSON.

confidence: high
steps: 6 of 10, urls retrieved: 11
```

Search and page reading are [Firecrawl](https://firecrawl.dev), which returns pages as markdown — so the program never parses HTML.

### What is enforced in code

- **The answer must come through `submit_answer`.** Prose does not end the loop; the agent is told so and asked again.
- **Citations are checked against what was actually retrieved.** A URL that this run never fetched is stripped from the sources and the answer is marked as having lost it. Confidently citing a plausible URL it never read is the characteristic failure of research agents.
- **Reading is confined to hosts that search surfaced**, so it cannot wander onto a domain it invented — but it may navigate freely within a site it legitimately found.
- **Running out of budget produces an answer, not silence.** On the last step it is asked for what it has, with the gaps named in `unresolved`.

### A survey it produced

`docs/unikernel-survey.md` is the output of running this agent twenty times, once per unikernel
project: language, licence, latest release, last commit, hypervisors, network stack, TLS, libc and
status, with the sources for each. Eight of the twenty are still active; only three could be shown
to support TLS.

The raw answers and the driver are in `docs/unikernel-survey/`, including the two failures worth
knowing about — self-inflicted rate limiting, and a ten-field question that exhausted the step
budget before the agent would commit.

### What it costs

`docs/agent-cost-report.html` measures 80 real research units — 22 minutes, 79 answered, every
unit's tokens, calls and fetches recorded, with Firecrawl consumption read from its credit
endpoint rather than estimated. Raw per-unit data is in `docs/agent-cost/units.tsv`.

Two findings that were not obvious before measuring: input tokens outweigh output **51 to 1**,
because the loop re-sends the conversation each step — so prompt caching is the difference between
two bills, not an optimisation. And **76% of the cost is search and page fetching**, not the model.

### Two things learned by watching it fail

The first version required a read URL to have appeared *exactly* in a search result. That sounded safe and was useless: the fact lived in `setup.sh`, a repository landing page does not contain it, and a file inside a repo never appears as its own search result. The agent could see where the answer was and was forbidden to go there. Same-host is the rule that keeps the property worth having.

It then still failed, because it kept searching for a page that would summarise the file instead of opening the file. That one was not a guardrail problem but a reasoning gap, so it was fixed in the prompt — the distinction is worth keeping straight: guardrails in code, judgement in the prompt.

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
