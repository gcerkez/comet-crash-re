#!/usr/bin/env python3
"""Recover function -> string references by disassembling the PPC64 code.

Ghidra's database has no references for this binary's string accesses:
game code loads string pointers from TOC slots (`lwz rX, disp(r2)` with
TOC base 0x1FC7C0, from the entry OPD), and Ghidra never established the
r2 context. So this script does it directly with Capstone:

  * disassemble the executable sections,
  * for every `lwz/ld/addi ..., disp(r2)` compute slot = TOC + disp,
  * also track simple `lis` + `addi/ori` absolute address materialization,
  * if the resulting address is a known string-pointer slot (from
    find_string_slots.py) or a defined string itself, record
    (function, string) using the corpus function ranges.

Usage: toc_string_xrefs.py <EBOOT.ELF> <functions.tsv> <string_slots_map.tsv>
       <strings.tsv> <out function_strings.tsv>
"""

import bisect
import re
import struct
import sys

from capstone import CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN, Cs

TOC_BASE = 0x1FC7C0
# Executable file-backed range: sections 1-4, VA 0x10200..0x1748E4,
# file offset = VA - 0x10000 in the first LOAD segment.
CODE_VA_START = 0x10200
CODE_VA_END = 0x1748E4


def parse_int(s: str) -> int:
    s = s.strip()
    if s.startswith("-0x"):
        return -int(s[3:], 16)
    if s.startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def main() -> int:
    elf_path, funcs_path, slots_path, strings_path, out_path = sys.argv[1:6]
    data = open(elf_path, "rb").read()

    # Function ranges from the corpus.
    starts, ranges = [], {}
    for line in open(funcs_path).read().splitlines()[1:]:
        p = line.split("\t")
        try:
            addr, size = int(p[0], 16), int(p[2])
        except (ValueError, IndexError):
            continue
        starts.append(addr)
        ranges[addr] = size
    starts.sort()

    def function_of(va: int):
        i = bisect.bisect_right(starts, va) - 1
        if i < 0:
            return None
        s = starts[i]
        return s if va < s + ranges[s] else None

    slot_to_string = {}
    for line in open(slots_path).read().splitlines()[1:]:
        p = line.split("\t")
        if len(p) == 3:
            slot_to_string[int(p[0], 16)] = (int(p[1], 16), p[2])

    string_values = {}
    for line in open(strings_path).read().splitlines()[1:]:
        p = line.split("\t")
        if len(p) == 3:
            try:
                string_values[int(p[0], 16)] = p[2]
            except ValueError:
                pass

    code = data[CODE_VA_START - 0x10000 : CODE_VA_END - 0x10000]
    md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
    md.detail = False
    md.skipdata = True  # literal pools/jump tables live inside .text

    mem_re = re.compile(r"^(\w+), (-?0x[0-9a-f]+|-?\d+)\((\w+)\)$")
    imm_re = re.compile(r"^(\w+), (\w+), (-?0x[0-9a-f]+|-?\d+)$")
    lis_re = re.compile(r"^(\w+), (-?0x[0-9a-f]+|-?\d+)$")

    rows = set()
    reg_addr = {}  # register -> materialized absolute address (lis tracking)
    for insn in md.disasm(code, CODE_VA_START):
        m = mem_re.match(insn.op_str)
        if insn.mnemonic in ("lwz", "ld", "lbz", "lhz", "lha", "addi") and m and m.group(3) == "r2":
            slot = TOC_BASE + parse_int(m.group(2))
            if slot in slot_to_string:
                fn = function_of(insn.address)
                if fn is not None:
                    sva, sval = slot_to_string[slot]
                    rows.add((fn, sva, sval))
            reg_addr.pop(m.group(1), None)
            continue
        if insn.mnemonic == "addi":
            m4 = imm_re.match(insn.op_str)
            if m4 and m4.group(2) == "r2":
                slot = TOC_BASE + parse_int(m4.group(3))
                if slot in slot_to_string:
                    fn = function_of(insn.address)
                    if fn is not None:
                        sva, sval = slot_to_string[slot]
                        rows.add((fn, sva, sval))
                reg_addr.pop(m4.group(1), None)
                continue
        if insn.mnemonic == "lis":
            m2 = lis_re.match(insn.op_str)
            if m2:
                reg_addr[m2.group(1)] = (parse_int(m2.group(2)) & 0xFFFF) << 16
            continue
        if insn.mnemonic in ("addi", "ori") :
            m3 = imm_re.match(insn.op_str)
            if m3 and m3.group(2) in reg_addr:
                base = reg_addr[m3.group(2)]
                lo = parse_int(m3.group(3))
                if insn.mnemonic == "addi":
                    # capstone prints the addi immediate signed; the
                    # lis+addi pair sums to the target address directly.
                    addr = base + lo
                else:
                    addr = base | (lo & 0xFFFF)
                fn = function_of(insn.address)
                if fn is not None:
                    if addr in slot_to_string:
                        sva, sval = slot_to_string[addr]
                        rows.add((fn, sva, sval))
                    elif addr in string_values:
                        rows.add((fn, addr, string_values[addr]))
                reg_addr[m3.group(1)] = addr
                continue
        # Any other write to a tracked register invalidates it (approx.:
        # only track within short lis->use windows by clearing on calls).
        if insn.mnemonic in ("bl", "bctrl", "blr"):
            reg_addr.clear()

    with open(out_path, "w") as f:
        f.write("function\tstring_address\tvalue\n")
        for fn, sva, sval in sorted(rows):
            f.write(f"{fn:08x}\t{sva:08x}\t{sval}\n")
    funcs = {r[0] for r in rows}
    print(f"{len(rows)} function-string references across {len(funcs)} functions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
