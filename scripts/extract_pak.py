#!/usr/bin/env python3
"""Extract a Comet Crash .pak archive (e.g. USRDIR/data/data2.pak).

Format (reverse-engineered 2026-11-05; `file` calls this "TTComp",
which is a false positive — the payload is stored raw):

    u32 total_size          (== file size)
    u32 file_count
    file_count x record:
        u32 name_offset     (absolute, into the name table that
                             immediately follows the record table)
        u32 data_offset     (absolute; entries are 0x80-aligned)
        u32 data_size       (exact byte length of the member)
    name table: NUL-terminated names, in record order
    data area: member bytes at data_offset, zero padding to the next
               0x80 boundary between members

Usage: extract_pak.py <archive.pak> <out dir>
Writes the members plus an index.tsv (name, offset, size).
"""

import os
import struct
import sys


def main() -> int:
    pak_path, out_dir = sys.argv[1], sys.argv[2]
    data = open(pak_path, "rb").read()
    total, count = struct.unpack(">II", data[:8])
    assert total == len(data), f"header size {total} != file size {len(data)}"

    records = []
    for i in range(count):
        name_off, data_off, data_size = struct.unpack(
            ">III", data[8 + i * 12 : 20 + i * 12]
        )
        end = data.index(b"\x00", name_off)
        name = data[name_off:end].decode("utf-8", "replace")
        records.append((name, data_off, data_size))

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "index.tsv"), "w") as idx:
        idx.write("name\toffset\tsize\n")
        for name, data_off, data_size in records:
            blob = data[data_off : data_off + data_size]
            assert len(blob) == data_size, f"short read for {name}"
            # Members go by basename only; the archive stores flat names.
            safe = os.path.basename(name)
            with open(os.path.join(out_dir, safe), "wb") as f:
                f.write(blob)
            idx.write(f"{safe}\t{data_off}\t{data_size}\n")
            print(f"{safe}  ({data_size} bytes @ {data_off:#x})")
    print(f"extracted {count} files from {pak_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
