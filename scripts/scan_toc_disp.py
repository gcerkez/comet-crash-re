# scan_toc_disp.py - boundary-free sweep for instructions
# using r2 (TOC) with a given displacement: finds loads AND
# stores of a TOC slot even when the access is a single
# instruction (disp within +-32768 needs no addis/addi pair,
# which is why scan_raw_xrefs.py missed these).
# args: ELF functions.tsv dispHex [dispHex...]
import sys, bisect
from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN
from capstone.ppc import PPC_OP_MEM, PPC_OP_REG

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
        if a <= va < a + s: return "%s@%x" % (n, a)
        return "gap@%x" % va
    return "?"
targets = set(int(x, 16) for x in sys.argv[3:])
md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
md.skipdata = True
LO, HI = 0x10230, 0x17305F
for ins in md.disasm(data[LO - 0x10000: HI - 0x10000], LO):
    if ins.id == 0:
        continue
    for op in ins.operands:
        if op.type == PPC_OP_MEM and op.mem.base != 0:
            base = ins.reg_name(op.mem.base)
            disp = op.mem.disp
            if base == "r2" and abs(disp) in targets:
                print("%08x  %-28s %s   [%s]" % (ins.address, ins.mnemonic + " " + ins.op_str, hex(disp & 0xFFFFFFFF), owner(ins.address)))
