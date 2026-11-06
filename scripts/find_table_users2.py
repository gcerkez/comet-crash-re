#!/usr/bin/env python3
"""v2: track r2-derived and lis-derived address values per function
(linear scan) to find consumers of the asset-registry pointer
slots (fonts/shaders/models) around 0x1F6100-0x1F6400."""
import csv
import struct
import sys

from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN

ELF, FUNCTIONS, STRINGS = sys.argv[1], sys.argv[2], sys.argv[3]
TOC = 0x1FC7C0
data = open(ELF, "rb").read()


def va2off(va):
    return va - 0x10000


strings = {}
for row in csv.reader(open(STRINGS), delimiter="\t"):
    if len(row) >= 3:
        try:
            strings[int(row[0], 16)] = row[2]
        except ValueError:
            pass

slots = {}
for va in range(0x1F6100, 0x1F6400, 4):
    (val,) = struct.unpack(">I", data[va2off(va):va2off(va) + 4])
    if val in strings:
        slots[va] = strings[val]

funcs = []
for row in csv.reader(open(FUNCTIONS), delimiter="\t"):
    if len(row) >= 3:
        try:
            funcs.append((int(row[0], 16), int(row[2])))
        except ValueError:
            pass

md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
md.skipdata = True

hits = {}
for start, size in funcs:
    if size <= 0 or size > 0x20000:
        continue
    regs = {}
    for insn in md.disasm(data[va2off(start):va2off(start) + size], start):
        try:
            ops = insn.operands
            mn = insn.mnemonic
            if mn == "addi" and len(ops) == 3 and ops[1].type == 1 and ops[2].type == 2:
                src = insn.reg_name(ops[1].reg)
                dst = insn.reg_name(ops[0].reg)
                if src == "r2":
                    ea = TOC + ops[2].imm
                    if ea in slots:
                        hits.setdefault(start, set()).add(slots[ea])
                    regs[dst] = ea
                elif src in regs:
                    regs[dst] = regs[src] + ops[2].imm
                else:
                    regs.pop(dst, None)
            elif mn == "lis" and len(ops) == 2 and ops[1].type == 2:
                regs[insn.reg_name(ops[0].reg)] = (ops[1].imm & 0xFFFF) << 16
            elif mn in ("ld", "lwz", "lhz", "lbz") and len(ops) == 2 and ops[1].type == 3:
                base = insn.reg_name(ops[1].mem.base)
                ea = None
                if base == "r2":
                    ea = TOC + ops[1].mem.disp
                elif base in regs:
                    ea = regs[base] + ops[1].mem.disp
                if ea in slots:
                    hits.setdefault(start, set()).add(slots[ea])
        except Exception:
            pass

for fn in sorted(hits):
    names = sorted(hits[fn])
    short = ", ".join(n.rsplit("/", 1)[-1] for n in names[:14])
    print("FUN_%08x: %d: %s" % (fn, len(names), short))
print("total consumer functions:", len(hits))
