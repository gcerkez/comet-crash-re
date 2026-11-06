#!/usr/bin/env python3
"""Census of uncarved code: coverage of the text segment by the
corpus function list, the gaps between carved bodies, and
boundary-free entry candidates inside the gaps:
  - targets of bl / b anywhere in the text that land in a gap
  - LR-save prologues (mflr r0) in a gap right after a blr,
    a nop, or zero padding
Writes gaps.tsv and entries_all.txt next to the corpus.
"""
import csv
import sys

from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN

ELF, FUNCTIONS, OUT_GAPS, OUT_ENTRIES = sys.argv[1:5]
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

# coverage + gaps
gaps = []
covered = 0
cursor = TEXT_LO
for s, z in funcs:
    if s > cursor:
        gaps.append((cursor, s))
    if s + z > cursor:
        covered += max(0, min(s + z, TEXT_HI) - max(s, TEXT_LO))
        cursor = max(cursor, s + z)
if cursor < TEXT_HI:
    gaps.append((cursor, TEXT_HI))
gap_set = [(a, b) for a, b in gaps if b - a >= 16]
print("text bytes: %d, covered by corpus: %d (%.1f%%), gaps >=16B: %d, gap bytes: %d"
      % (TEXT_HI - TEXT_LO, covered, 100.0 * covered / (TEXT_HI - TEXT_LO),
         len(gap_set), sum(b - a for a, b in gap_set)))


def in_gap(va):
    for a, b in gap_set:
        if a <= va < b:
            return (a, b)
    return None


md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
md.skipdata = True

entries = {}  # va -> evidence
prev_word = 0
for insn in md.disasm(data[va2off(TEXT_LO):va2off(TEXT_HI)], TEXT_LO):
    va = insn.address
    try:
        g = in_gap(va)
        if insn.mnemonic in ("bl",) and insn.operands and insn.operands[0].type == 2:
            t = insn.operands[0].imm & 0xFFFFFFFF
            if in_gap(t):
                entries.setdefault(t, set()).add(insn.mnemonic + "-target")
        if g is not None and insn.mnemonic == "mflr" and insn.op_str.strip() == "r0":
            if prev_word in (0x4E800020, 0x60000000, 0x00000000):
                entries.setdefault(va, set()).add("prologue-after-return")
    except Exception:
        pass
    raw = data[va2off(va):va2off(va) + 4]
    if len(raw) == 4:
        prev_word = int.from_bytes(raw, "big")

with open(OUT_GAPS, "w") as f:
    f.write("gap_start\tgap_end\tsize\n")
    for a, b in gap_set:
        f.write("%08x\t%08x\t%d\n" % (a, b, b - a))
with open(OUT_ENTRIES, "w") as f:
    for va in sorted(entries):
        f.write("%x\n" % va)
print("entry candidates in gaps:", len(entries))
big = sorted(gap_set, key=lambda g: g[1] - g[0], reverse=True)[:15]
for a, b in big:
    inside = [e for e in entries if a <= e < b]
    print("gap %08x-%08x  %6d bytes  %d candidate entries" % (a, b, b - a, len(inside)))
