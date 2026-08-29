#!/usr/bin/env python3
"""Summarise results.tsv: median and spread per (platform, test, opt).

Medians rather than means, and the min-max spread alongside, because bmhunt has
two shared vCPUs and a single run off a shared box is noise. Anything reported
as a single number would be a number about scheduling luck."""
import collections, statistics, sys

rows = []
with open(sys.argv[1]) as f:
    hdr = f.readline().rstrip("\n").split("\t")
    for line in f:
        rows.append(dict(zip(hdr, line.rstrip("\n").split("\t"))))

g = collections.defaultdict(list)
for r in rows:
    g[(r["platform"], r["test"], r["opt"])].append(r)

def num(x):
    try: return float(x)
    except (TypeError, ValueError): return None

print(f"{'platform':<11}{'test':<11}{'opt':<5}{'n':<4}"
      f"{'boot→start s':>14}{'compute s':>12}{'spread':>16}  result")
for k in sorted(g):
    rs = g[k]
    starts  = [v for v in (num(r["start_s"]) for r in rs) if v is not None]
    computes = [b - a for a, b in
                ((num(r["start_s"]), num(r["done_s"])) for r in rs)
                if a is not None and b is not None]
    res = rs[0]["result"]
    if not computes:
        print(f"{k[0]:<11}{k[1]:<11}{k[2]:<5}{len(rs):<4}{'':>14}{'no result':>12}")
        continue
    print(f"{k[0]:<11}{k[1]:<11}{k[2]:<5}{len(rs):<4}"
          f"{statistics.median(starts):>14.4f}"
          f"{statistics.median(computes):>12.3f}"
          f"{min(computes):>7.3f}–{max(computes):<8.3f}  {res[:60]}")
