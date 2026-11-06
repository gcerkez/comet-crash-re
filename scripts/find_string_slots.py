#!/usr/bin/env python3
"""Find the pointer slots through which code references defined strings.

In this PPC64 ELF, string addresses are materialized via 32-bit pointer
slots in the writable LOAD segment (TOC-style addressing). This scans
that segment for words equal to a defined string's address (from the
corpus strings.tsv) and writes:
  <out_slots.txt>  one slot VA per line (input for ExportStringXrefs.java)
  <out_map.tsv>    slot VA, string VA, string value

Usage: find_string_slots.py <EBOOT.ELF> <strings.tsv> <out_slots.txt> <out_map.tsv>
"""

import struct
import sys

# Writable LOAD segment of this ELF (VA 0x1F0000, file offset 0x1E0000).
SEG_VA = 0x1F0000
SEG_OFF = 0x1E0000
SEG_SIZE = 0xCCC4C


def main() -> int:
    elf_path, strings_path, slots_path, map_path = sys.argv[1:5]
    data = open(elf_path, "rb").read()
    strings = {}
    for line in open(strings_path).read().splitlines()[1:]:
        parts = line.split("\t")
        if len(parts) == 3:
            try:
                strings[int(parts[0], 16)] = parts[2]
            except ValueError:
                pass  # Ghidra pseudo-addresses (SECTIONnn::...) are not VAs

    rows = []
    for off in range(SEG_OFF, SEG_OFF + SEG_SIZE - 4, 4):
        (value,) = struct.unpack(">I", data[off : off + 4])
        if value in strings:
            slot_va = SEG_VA + (off - SEG_OFF)
            rows.append((slot_va, value, strings[value]))

    with open(slots_path, "w") as f:
        for slot_va, _v, _s in rows:
            f.write(f"{slot_va:x}\n")
    with open(map_path, "w") as f:
        f.write("slot\tstring_address\tvalue\n")
        for slot_va, value, s in rows:
            f.write(f"{slot_va:08x}\t{value:08x}\t{s}\n")
    print(f"{len(rows)} string-pointer slots")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
