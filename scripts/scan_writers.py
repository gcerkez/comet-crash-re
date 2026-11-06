#!/usr/bin/env python3
"""Boundary-free sweep for STORES into a VA window: who WRITES
the view-state / defaults fields? Tracks r2-relative stores and
stores through registers holding tracked addresses."""
import csv
import sys

from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN

ELF, FUNCTIONS = sys.argv[1], sys.argv[2]
WIN_LO, WIN_HI = int(sys.argv[3], 16), int(sys.argv[4], 16)
TOC = 0x1FC7C0
TEXT_LO, TEXT_HI = 0x10230, 0x173060
data = open(ELF, "rb").read()


def va2off(va):
    return va - 0x10000


funcs = []
for row in csv.reader(open(FUNCTIONS), delimiter="\t"):
    if len(row) >= 3:
        try:
            funcs.append((int(row[0], 16), int(row[2]), row[1]))
        except ValueError:
            pass
funcs.sort()


def owner(va):
    for s, z, n in funcs:
        if s <= va < s + z:
            return n
    return "gap"


md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
md.skipdata = True

STORES = ("stw", "stfs", "stfd", "std", "stb", "sth", "stwu", "stfdu", "stwx", "stfsx")
hits = {}
regs = {}
for insn in md.disasm(data[va2off(TEXT_LO):va2off(TEXT_HI)], TEXT_LO):
    try:
        ops = insn.operands
        mn = insn.mnemonic
        if mn == "addi" and len(ops) == 3 and ops[1].type == 1 and ops[2].type == 2:
            src = insn.reg_name(ops[1].reg)
            dst = insn.reg_name(ops[0].reg)
            if src == "r2":
                regs[dst] = TOC + ops[2].imm
            elif src in regs:
                regs[dst] = regs[src] + ops[2].imm
            else:
                regs.pop(dst, None)
        elif mn == "lis" and len(ops) == 2 and ops[1].type == 2:
            regs[insn.reg_name(ops[0].reg)] = (ops[1].imm & 0xFFFF) << 16
        elif mn in STORES and len(ops) == 2 and ops[1].type == 3:
            base = insn.reg_name(ops[1].mem.base)
            ea = None
            if base == "r2":
                ea = TOC + ops[1].mem.disp
            elif base in regs:
                ea = regs[base] + ops[1].mem.disp
            if ea is not None and WIN_LO <= ea < WIN_HI:
                hits.setdefault(insn.address, set()).add(ea)
    except Exception:
        pass

for va in sorted(hits):
    targets = ", ".join(hex(x) for x in sorted(hits[va]))
    print("%08x  %-18s -> %s" % (va, owner(va), targets))
print("writer instructions:", len(hits))
