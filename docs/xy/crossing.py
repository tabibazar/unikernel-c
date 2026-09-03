#!/usr/bin/env python3
"""Find T_KT from an xy.c sweep.

The Kosterlitz-Thouless prediction is not a temperature, it is a line: the
helicity modulus Upsilon drops through 2T/pi, and wherever it crosses is
T_KT. So the estimate comes from intersecting two curves, not from locating a
peak the way a conventional critical point is found.

Linear interpolation between the two bracketing temperatures is enough. The
real uncertainty here is not the interpolation but the finite lattice: the
crossing drifts down towards 0.8929 as L grows, and only logarithmically, so
three sizes show the direction of the drift rather than extrapolating to it.

    ./crossing.py xy16.txt xy32.txt xy64.txt
"""
import sys, math

def crossing(path):
    rows = []
    for line in open(path):
        if not line.startswith("XY_T"):
            continue
        f = line.split()
        rows.append((float(f[1]), float(f[3]), float(f[4])))   # T, upsilon, 2T/pi
    if len(rows) < 2:
        return None, rows
    for (t0, u0, l0), (t1, u1, l1) in zip(rows, rows[1:]):
        d0, d1 = u0 - l0, u1 - l1
        if d0 >= 0 >= d1:                      # sign change: the crossing
            if d0 == d1:
                return t0, rows
            return t0 + (t1 - t0) * d0 / (d0 - d1), rows
    return None, rows

print(f"{'file':<14}{'L':>5}{'T_KT (crossing)':>18}{'points':>9}")
for p in sys.argv[1:]:
    tc, rows = crossing(p)
    L = "?"
    for line in open(p):
        if line.startswith("XY_START"):
            L = line.split("L=")[1].split()[0]
            break
    shown = f"{tc:.4f}" if tc else "no crossing yet"
    print(f"{p:<14}{L:>5}{shown:>18}{len(rows):>9}")

print("\naccepted value, L -> infinity: T_KT = 0.8929")
print("finite lattices cross above it and converge logarithmically")
