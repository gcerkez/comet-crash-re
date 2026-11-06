# scan_callers.py - boundary-free caller map: every bl/b
# whose target falls in a given address range, reported
# with the containing function (or gap). args: ELF
# functions.tsv loHex hiHex
import sys, bisect, struct
data = open(sys.argv[1], "rb").read()
funcs = []
for ln in open(sys.argv[2]):
    p = ln.rstrip("\n").split("\t")
    if len(p) >= 3:
        try: funcs.append((int(p[0], 16), int(p[2]), p[1]))
        except ValueError: pass
funcs.sort()
starts = [f[0] for f in funcs]
def owner(va):
    i = bisect.bisect_right(starts, va) - 1
    if i >= 0:
        a, s, n = funcs[i]
        if a <= va < a + s: return n
    return "GAP"
lo, hi = int(sys.argv[3], 16), int(sys.argv[4], 16)
LO, HI = 0x10230, 0x17305F
from collections import Counter
hits = Counter()
detail = {}
for va in range(LO, HI, 4):
    w = struct.unpack(">I", data[va - 0x10000: va - 0x10000 + 4])[0]
    if (w >> 26) in (0x12, 0x13):
        imm = w & 0x3FFFFFC
        if imm & 0x2000000: imm -= 0x4000000
        tgt = (va + imm) & 0xFFFFFFFF
        if lo <= tgt < hi:
            o = owner(va)
            hits[o] += 1
            detail.setdefault(o, []).append((va, tgt))
for o, c in hits.most_common():
    tgts = sorted(set(t for _, t in detail[o]))
    print("%-28s x%-3d -> %s" % (o, c, ", ".join("%x" % t for t in tgts[:6])))
