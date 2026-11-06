#!/usr/bin/env python3
"""Find functions that load given TOC slots (r2-relative).

Usage: find_slot_users.py <elf> <functions.tsv> <slot-va> [slot-va...]
Prints FUN_xxxxxxxx for each function containing a load whose
effective address (TOC + disp) equals one of the slots, including
the addi-address-then-load pattern via a small register tracker.
"""
import csv
import struct
import sys

from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN

ELF, FUNCTIONS = sys.argv[1], sys.argv[2]
WANT = {int(a, 16) for a in sys.argv[3:]}
TOC = 0x1FC7C0
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

md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
md.skipdata = True

for start, size in funcs:
    if size <= 0 or size > 0x20000:
        continue
    regs = {}
    found = set()
    for insn in md.disasm(data[va2off(start):va2off(start) + size], start):
        try:
            ops = insn.operands
            mn = insn.mnemonic
            if mn == "addi" and len(ops) == 3 and ops[1].type == 1 and ops[2].type == 2:
                src = insn.reg_name(ops[1].reg)
                dst = insn.reg_name(ops[0].reg)
                if src == "r2":
                    ea = TOC + ops[2].imm
                    if ea in WANT:
                        found.add(ea)
                    regs[dst] = ea
                elif src in regs:
                    regs[dst] = regs[src] + ops[2].imm
                else:
                    regs.pop(dst, None)
            elif mn in ("ld", "lwz", "lhz", "lbz") and len(ops) == 2 and ops[1].type == 3:
                base = insn.reg_name(ops[1].mem.base)
                ea = None
                if base == "r2":
                    ea = TOC + ops[1].mem.disp
                elif base in regs:
                    ea = regs[base] + ops[1].mem.disp
                if ea in WANT:
                    found.add(ea)
        except Exception:
            pass
    if found:
        print("FUN_%08x uses %s" % (start, ", ".join(hex(x) for x in sorted(found))))
