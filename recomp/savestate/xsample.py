#!/usr/bin/env python3
"""Extended sampler: svtools.sample + bank CRC32s, live-entity census, ship record pos."""
import sys, struct, zlib, json
sys.path.insert(0, "/home/gcerkez/cc-savestate-work")
from svtools import Guest, OBJ, sample

pid = int(sys.argv[1])
s = sample(pid)
g = Guest(pid)
bankA = g.u32(OBJ + 0x180); bankB = g.u32(OBJ + 0x198)
dataA = g.read(bankA, 1024 * 256); dataB = g.read(bankB, 1024 * 256)
s["bankA_crc32"] = "%08x" % zlib.crc32(dataA)
s["bankB_crc32"] = "%08x" % zlib.crc32(dataB)
s["census_live_bankA"] = sum(1 for i in range(1024) if dataA[i * 256 + 0x0F] not in (0, 63))
idx = s["ship_index"]
x, _y, z, _w = struct.unpack(">4f", dataA[idx * 256: idx * 256 + 16])
s["ship_rec_x"] = x
s["ship_rec_z"] = z
print(json.dumps(s, indent=1))
