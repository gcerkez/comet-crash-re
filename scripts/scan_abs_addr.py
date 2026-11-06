# scan_abs_addr.py - find code forming an absolute address
# via addis+addi/ori pairs (the way a BSS buffer address is
# materialized). args: ELF functions.tsv targetHex [targetHex]
import sys, bisect
from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN

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
targets = set(int(x, 16) for x in sys.argv[3:])
md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.skipdata = True
LO, HI = 0x10230, 0x17305F
insns = list(md.disasm(data[LO - 0x10000: HI - 0x10000], LO))
for i, ins in enumerate(insns):
    if ins.mnemonic not in ("addis", "lis"): continue
    parts = ins.op_str.replace(",", " ").split()
    if len(parts) < 3: continue
    reg = parts[0]
    try: hi = int(parts[2], 0)
    except ValueError: continue
    if hi >= 0x8000: hi -= 0x10000
    for j in range(i + 1, min(i + 7, len(insns))):
        n = insns[j]
        if n.mnemonic not in ("addi", "ori", "addis", "lis"): continue
        p2 = n.op_str.replace(",", " ").split()
        if len(p2) < 3 or p2[1] != reg: continue
        try: lo = int(p2[2], 0)
        except ValueError: continue
        if n.mnemonic == "ori":
            addr = ((hi << 16) | (lo & 0xFFFF)) & 0xFFFFFFFF
        else:
            if lo >= 0x8000: lo -= 0x10000
            addr = ((hi << 16) + lo) & 0xFFFFFFFF
        if addr in targets:
            print("%08x  forms %08x via %s ; %s   [%s]" % (ins.address, addr, ins.mnemonic + " " + ins.op_str, n.mnemonic + " " + n.op_str, owner(ins.address)))
        break
