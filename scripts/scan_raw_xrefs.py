#!/usr/bin/env python3
"""Raw whole-text sweep for references to a VA window, with no
function-boundary assumptions: catches consumers hiding in code
Ghidra never carved into functions. Patterns: r2-relative loads
and addi address formation whose EA lands in the window, and
lis/addi absolute address formation into the window."""
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
            funcs.append((int(row[0], 16), int(row[2])))
        except ValueError:
            pass
funcs.sort()


def owner(va):
    best = None
    for start, size in funcs:
        if start <= va < start + size:
            return "FUN_%08x" % start
        if start <= va:
            best = start
    return "gap(after FUN_%08x)" % best if best else "gap"


md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
md.skipdata = True

code = data[va2off(TEXT_LO):va2off(TEXT_HI)]
hits = {}
regs = {}
for insn in md.disasm(code, TEXT_LO):
    try:
        ops = insn.operands
        mn = insn.mnemonic
        if mn == "addi" and len(ops) == 3 and ops[1].type == 1 and ops[2].type == 2:
            src = insn.reg_name(ops[1].reg)
            dst = insn.reg_name(ops[0].reg)
            if src == "r2":
                ea = TOC + ops[2].imm
                if WIN_LO <= ea < WIN_HI:
                    hits.setdefault(insn.address, set()).add(ea)
                regs[dst] = ea
            elif src in regs:
                ea = regs[src] + ops[2].imm
                if WIN_LO <= ea < WIN_HI:
                    hits.setdefault(insn.address, set()).add(ea)
                regs[dst] = ea
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
            if ea is not None and WIN_LO <= ea < WIN_HI:
                hits.setdefault(insn.address, set()).add(ea)
    except Exception:
        pass

for va in sorted(hits):
    targets = ", ".join(hex(x) for x in sorted(hits[va]))
    print("%08x  %-28s -> %s" % (va, owner(va), targets))
print("total hit instructions:", len(hits))
