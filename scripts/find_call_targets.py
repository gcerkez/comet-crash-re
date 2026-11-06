#!/usr/bin/env python3
"""Collect bl/blrl call targets inside given VA ranges — call
targets are function entries by construction. Also list the
corpus functions already carved inside the ranges (the gaps
between them are the uncarved code)."""
import csv
import sys

from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN

ELF, FUNCTIONS = sys.argv[1], sys.argv[2]
RANGES = []
for a in sys.argv[3:]:
    lo, hi = a.split("-")
    RANGES.append((int(lo, 16), int(hi, 16)))
TEXT_LO, TEXT_HI = 0x10230, 0x173060
data = open(ELF, "rb").read()


def va2off(va):
    return va - 0x10000


funcs = []
for row in csv.reader(open(FUNCTIONS), delimiter="\t"):
    if len(row) >= 3:
        try:
            funcs.append((int(row[0], 16), int(row[2])))
        except ValueError:
            pass
funcs.sort()

md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
md.skipdata = True

targets = set()
for insn in md.disasm(data[va2off(TEXT_LO):va2off(TEXT_HI)], TEXT_LO):
    if insn.mnemonic in ("bl", "bcl") and insn.operands and insn.operands[0].type == 2:
        t = insn.operands[0].imm & 0xFFFFFFFF
        for lo, hi in RANGES:
            if lo <= t < hi:
                targets.add(t)

carved = set()
for s, z in funcs:
    for lo, hi in RANGES:
        if lo <= s < hi:
            carved.add(s)
print("# carved entries in ranges:", [hex(x) for x in sorted(carved)])
print("# call targets in ranges:", len(targets))
new = sorted(t for t in targets if t not in carved)
print("# NEW entry candidates:", len(new))
for t in new:
    print("%x" % t)
