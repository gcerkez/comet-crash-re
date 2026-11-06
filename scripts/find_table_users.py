#!/usr/bin/env python3
"""Find functions that consume the model-name pointer table.

The model path strings (0x17CFxx) are pointed to from a pointer
array in the RW segment around 0x1F61C0-0x1F6400, which sits
within signed-16-bit reach of the TOC (0x1FC7C0). Code therefore
loads those pointers with TOC-relative loads (base r2). This
scan maps every pointer slot in that window to its string, then
disassembles all corpus functions (Capstone, skipdata) looking
for r2-based loads whose effective address hits a slot.
"""
import csv
import struct
import sys

from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN

ELF = sys.argv[1]
FUNCTIONS = sys.argv[2]
STRINGS = sys.argv[3]
TOC = 0x1FC7C0
WIN_LO, WIN_HI = 0x1F6100, 0x1F6400

data = open(ELF, "rb").read()


def va2off(va):
    return va - 0x10000


strings = {}
with open(STRINGS) as f:
    for row in csv.reader(f, delimiter="\t"):
        if len(row) >= 3:
            try:
                strings[int(row[0], 16)] = row[2]
            except ValueError:
                pass

slots = {}  # slot VA -> string text
for va in range(WIN_LO, WIN_HI, 4):
    (val,) = struct.unpack(">I", data[va2off(va):va2off(va) + 4])
    if val in strings:
        slots[va] = strings[val]
print(f"# {len(slots)} pointer slots in window", file=sys.stderr)
for va in sorted(slots):
    print(f"# slot {va:#x} -> {slots[va]}", file=sys.stderr)

funcs = []
with open(FUNCTIONS) as f:
    for row in csv.reader(f, delimiter="\t"):
        if len(row) >= 2:
            try:
                start = int(row[0], 16)
                size = int(row[2])
            except ValueError:
                continue
            funcs.append((start, size))
funcs.sort()
print(f"# {len(funcs)} functions", file=sys.stderr)

md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
md.skipdata = True

hits = {}
for start, size in funcs:
    if size <= 0 or size > 0x20000:
        continue
    code = data[va2off(start):va2off(start) + size]
    for insn in md.disasm(code, start):
        if insn.mnemonic not in ("ld", "lwz", "lhz", "lbz", "addi", "lwzx"):
            continue
        for op in insn.operands:
            if op.type == 3:  # MEM
                base = op.mem.base
                disp = op.mem.disp
                # PPC capstone reg id for r2 == 2? rN ids are sequential
                # from the PPC_REG_R0 base; check via reg_name instead.
                if insn.reg_name(base) == "r2":
                    ea = TOC + disp
                    if ea in slots:
                        hits.setdefault(start, set()).add((ea, slots[ea]))
for fn in sorted(hits):
    names = sorted(hits[fn])
    print(f"FUN_{fn:08x} uses {len(names)} slot(s):")
    for ea, s in names:
        print(f"    {ea:#x} {s}")
