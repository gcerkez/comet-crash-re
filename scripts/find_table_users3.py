#!/usr/bin/env python3
"""v3: find consumers of the asset-registry table (0x1F61xx) by
tracking VALUES: a register that receives a load from any static
location whose stored u32 points into the table region is a
table base; any later load through it (disp or indexed) marks
the function as a consumer."""
import csv
import struct
import sys

from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN

ELF, FUNCTIONS, STRINGS = sys.argv[1], sys.argv[2], sys.argv[3]
TOC = 0x1FC7C0
TLO, THI = 0x1F6100, 0x1F6400
data = open(ELF, "rb").read()


def va2off(va):
    return va - 0x10000


def mem32(va):
    if va < 0x10000 or va2off(va) + 4 > len(data):
        return None
    (v,) = struct.unpack(">I", data[va2off(va):va2off(va) + 4])
    return v


strings = {}
for row in csv.reader(open(STRINGS), delimiter="\t"):
    if len(row) >= 3:
        try:
            strings[int(row[0], 16)] = row[2]
        except ValueError:
            pass

# Locations whose stored pointer aims into the table region, and
# the table slots themselves (value -> model/font string).
base_ptrs = {}
for va in range(0x1F0000, 0x210000, 4):
    v = mem32(va)
    if v is not None and TLO <= v < THI:
        base_ptrs[va] = v
slots = {}
for va in range(TLO, THI, 4):
    v = mem32(va)
    if v in strings:
        slots[va] = strings[v]
print("# base-pointer locations:", len(base_ptrs), "slots:", len(slots), file=sys.stderr)

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
    addr_regs = {}   # reg -> known absolute address it holds
    table_regs = set()  # regs known to hold a table-region pointer
    for insn in md.disasm(data[va2off(start):va2off(start) + size], start):
        try:
            ops = insn.operands
            mn = insn.mnemonic
            if mn == "addi" and len(ops) == 3 and ops[1].type == 1 and ops[2].type == 2:
                src = insn.reg_name(ops[1].reg)
                dst = insn.reg_name(ops[0].reg)
                if src == "r2":
                    addr_regs[dst] = TOC + ops[2].imm
                elif src in addr_regs:
                    addr_regs[dst] = addr_regs[src] + ops[2].imm
                else:
                    addr_regs.pop(dst, None)
                table_regs.discard(dst)
            elif mn == "lis" and len(ops) == 2 and ops[1].type == 2:
                dst = insn.reg_name(ops[0].reg)
                addr_regs[dst] = (ops[1].imm & 0xFFFF) << 16
                table_regs.discard(dst)
            elif mn in ("ld", "lwz", "lhz", "lbz", "lwzx", "ldx") and len(ops) == 2:
                dst = insn.reg_name(ops[0].reg)
                op = ops[1]
                if op.type == 3:  # MEM disp(base)
                    base = insn.reg_name(op.mem.base)
                    ea = None
                    if base == "r2":
                        ea = TOC + op.mem.disp
                    elif base in addr_regs:
                        ea = addr_regs[base] + op.mem.disp
                    if base in table_regs:
                        hits.setdefault(start, set()).add("via-table-base")
                    if ea is not None:
                        if ea in slots:
                            hits.setdefault(start, set()).add(slots[ea])
                        if ea in base_ptrs:
                            table_regs.add(dst)
                        addr_regs.pop(dst, None)
                    else:
                        addr_regs.pop(dst, None)
                elif op.type == 4:  # MEM indexed (rare in this binding)
                    hits.setdefault(start, set()).add("indexed?")
        except Exception:
            pass

for fn in sorted(hits):
    names = sorted(hits[fn])
    short = ", ".join(n.rsplit("/", 1)[-1] for n in names[:12])
    print("FUN_%08x: %s" % (fn, short))
print("total consumer functions:", len(hits))
