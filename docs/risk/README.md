# A Monte Carlo derivatives risk engine, on a unikernel

Prices a European option and measures the risk of a position, on a BareMetal
unikernel with no operating system, using the machine's own hardware
randomness — and validates itself against the exact Black–Scholes price.

## Results (on BareMetal, hardware RNG)

Instrument: 1-year at-the-money European call. S=100, K=100, r=5%, vol=20%.

| method | call price |
|---|---|
| **Black–Scholes (exact)** | **10.4506** |
| Monte Carlo, 8M paths, antithetic | **10.4521 ± 0.0026** |
| delta (both) | 0.6368 |

The simulation lands on the analytic price within one standard error. Two real
quant techniques are shown honestly:

- **Antithetic variates.** Pairing each random shock Z with −Z cancels noise, so
  the error bar halves without more samples: plain MC 10.4486 ± 0.0052 vs
  antithetic 10.4521 ± 0.0026 — a measured **4.0× variance reduction**.
- **Pathwise Greeks.** The option's delta (0.6368) comes out of the same
  simulation and matches Black–Scholes exactly.

**Value-at-Risk**, from the simulated 10-day loss distribution on a $1M
position: VaR95 = $62,372, VaR99 = $86,914, Expected Shortfall (ES99) = $98,985.
These come from the distribution itself, which no closed form gives you.

## Using every API BareMetal has

This is the compute core of a full pipeline, and every external service is
load-bearing — each already proven working in the deployed agent this session:

```
Telegram  ->  the instrument in plain English
Firecrawl ->  live market data over HTTPS
Gemini    ->  parse it into parameters
RDRAND    ->  the random price paths (proven-clean hardware entropy)
compute   ->  simulate, price, measure risk  (this file)
Redis     ->  an immutable, reproducible audit record
PNG + Telegram -> the chart and the answer back
```

## Why a unikernel

Return Infinity sells deterministic, auditable compute to finance. Two things
that story needs, and this shows:

- **Reproducibility.** Every run can be logged to Redis as an audit record —
  what MiFID II and SEC rules require. And reproducibility is not free on this
  platform: it silently computed wrong [1 in 4,000 times](../arithmetic-fault/)
  until we root-caused and fixed it. A risk number you cannot reproduce is one
  you cannot defend.
- **Sound randomness.** Millions of Gaussian shocks, each from `RDRAND`, the
  source [shown clean](../trng/) against ent, FIPS 140-2 and dieharder. A
  unikernel has no `/dev/random`; this one makes its own.

`exp`, `ln`, `sqrt`, the normal CDF (`erf`), and the Gaussian sampler (Marsaglia
polar) are all hand-rolled — a freestanding unikernel has no math library.

## Reproducing

```sh
cp risk.c BareMetal-App/ && ./1-build.sh risk.c    # boot the ELF under Firecracker
```
Measured on an AMD EPYC host under Firecracker 1.16.
