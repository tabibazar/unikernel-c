"""Rank OEIS hard+more counting sequences by how attackable the next term looks.

The four famous families all failed for the same reason -- fame attracts
hardware -- so this deliberately scores AGAINST any sequence that mentions a
cluster, GPU, FPGA or distributed project, and FOR sequences whose last
extension is old and whose growth per term is modest.
"""
import json, re, sys, datetime

MONTHS = {m: i+1 for i, m in enumerate(
    "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split())}
NOW = datetime.date(2026, 9, 4)

# Signals that someone with real hardware already owns this frontier.
HW = re.compile(r'\b(GPU|FPGA|cluster|supercomput|BOINC|distributed\.net|'
                r'PrimeGrid|CPU[- ]years?|core[- ]years?|CUDA|Xeon Phi|'
                r'parallel comput|HPC|petaflop|teraflop)\b', re.I)
# Methods that need one big shared state -- disqualifying for stateless workers.
MEMBOUND = re.compile(r'\b(transfer matrix|transfer-matrix|dynamic programming|'
                      r'DP over|meet.in.the.middle|BDD|ZDD|decision diagram)\b', re.I)

def last_ext_date(rec):
    """Most recent dated credit anywhere in the record's provenance fields."""
    best = None
    for field in ('ext', 'comment'):
        for line in rec.get(field, []) or []:
            for mon, day, yr in re.findall(r'\b(%s)\s+(\d{1,2})\s+(\d{4})\b'
                                           % "|".join(MONTHS), line):
                try:
                    d = datetime.date(int(yr), MONTHS[mon], int(day))
                except ValueError:
                    continue
                if best is None or d > best:
                    best = d
    return best

def growth(rec):
    """Ratio between the last two terms: how much bigger the next one gets."""
    try:
        t = [int(x) for x in rec['data'].split(',') if x.strip()]
    except ValueError:
        return None, 0
    if len(t) < 3:
        return None, len(t)
    a, b = t[-2], t[-1]
    if a <= 0:
        return None, len(t)
    return b / a, len(t)

def score(rec):
    txt = " ".join(str(rec.get(k, "")) for k in
                   ('name', 'comment', 'link', 'ext', 'formula'))
    hw = bool(HW.search(txt))
    mb = bool(MEMBOUND.search(txt))
    d = last_ext_date(rec)
    ratio, nterms = growth(rec)
    age = (NOW - d).days / 365.25 if d else None

    s = 0.0
    if age is not None:
        s += min(age, 20) * 2            # stale is good, capped at 20 years
    if hw:  s -= 40                      # someone owns this with hardware
    if mb:  s -= 25                      # needs shared state we cannot provide
    if ratio is not None:
        # a next term 2-50x the last is plausible; 1e6x is not
        if ratio < 1.5:      s += 4
        elif ratio < 10:     s += 10
        elif ratio < 100:    s += 6
        elif ratio < 10000:  s += 1
        else:                s -= 12
    if nterms and nterms <= 14: s += 6   # few terms => each one is expensive
    return s, dict(a="A%06d" % rec['number'], age=round(age,1) if age else None,
                   nterms=nterms, ratio=round(ratio,1) if ratio else None,
                   hw=hw, membound=mb, name=rec.get('name','')[:96])

recs = json.load(open(sys.argv[1] if len(sys.argv)>1 else 'oeis_raw.json'))
seen=set(); rows=[]
for r in recs:
    if r['number'] in seen: continue
    seen.add(r['number'])
    s, info = score(r); info['score']=round(s,1); rows.append(info)
rows.sort(key=lambda x: -x['score'])

print(f"harvested {len(recs)}, unique {len(seen)}")
print(f"{'seq':<9}{'score':>6}{'age_y':>7}{'terms':>6}{'ratio':>9}  name")
for r in rows[:30]:
    flag = ('HW' if r['hw'] else '') + ('MB' if r['membound'] else '')
    print(f"{r['a']:<9}{r['score']:>6}{str(r['age']):>7}{r['nterms']:>6}"
          f"{str(r['ratio']):>9}  {flag:<3}{r['name']}")
json.dump(rows, open('oeis_ranked.json','w'), indent=1)
print("\nfull ranking -> oeis_ranked.json")
