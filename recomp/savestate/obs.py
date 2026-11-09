#!/usr/bin/env python3
"""obs.py -- read Comet Crash observables from a running recomp process.

Usage: obs.py <pid>
Prints JSON: guest state/mode/mission, per-slot Thorium/life/stockpiles,
the human ship record (bank, index, health, pos), entity census per bank
and a byte-sum over each bank (restore-fidelity check).

Guest addressing: vm_base = start of the 0x100010000-sized rw mapping in
/proc/<pid>/maps; file offsets in /proc/<pid>/mem are vm_base+guest_addr.
All multi-byte guest values are big-endian.
"""
import json
import struct
import sys

OBJ = 0x30E600
VM_SPAN = 0x100010000


def find_vm_base(pid):
    with open(f"/proc/{pid}/maps") as f:
        for line in f:
            parts = line.split()
            lo, hi = (int(x, 16) for x in parts[0].split("-"))
            if hi - lo == VM_SPAN and "rw" in parts[1]:
                return lo
    raise SystemExit("vm mapping not found")


class Guest:
    def __init__(self, pid):
        self.base = find_vm_base(pid)
        self.mem = open(f"/proc/{pid}/mem", "rb", buffering=0)

    def read(self, addr, n):
        self.mem.seek(self.base + addr)
        return self.mem.read(n)

    def u32(self, addr):
        return struct.unpack(">I", self.read(addr, 4))[0]

    def i16(self, addr):
        return struct.unpack(">h", self.read(addr, 2))[0]

    def u16(self, addr):
        return struct.unpack(">H", self.read(addr, 2))[0]

    def f32(self, addr):
        return struct.unpack(">f", self.read(addr, 4))[0]


def main():
    pid = int(sys.argv[1])
    g = Guest(pid)
    out = {
        "state": g.u32(OBJ + 0x5520C0),
        "mode": g.u32(OBJ + 0x55209C),
        "mission": g.u32(OBJ + 0x551EBC),
        "slots": [],
        "banks": [],
    }
    for slot in range(4):
        blk = OBJ + 0x300 + slot * 0x80
        out["slots"].append({
            "thorium": g.f32(blk + 0x48),
            "life": g.i16(blk + 0x58),
            "stockpiles": [g.i16(blk + 0x64 + k * 2) for k in range(8)],
        })
    banks = [g.u32(OBJ + 0x180), g.u32(OBJ + 0x198)]
    for b, bank in enumerate(banks):
        if not bank:
            out["banks"].append({"base": 0})
            continue
        data = g.read(bank, 1024 * 256)
        census = {}
        byte_sum = 0
        for i in range(1024):
            t = data[i * 256 + 0x0F]
            if t:
                census[t] = census.get(t, 0) + 1
        for i in range(0, len(data), 8):
            byte_sum = (byte_sum + struct.unpack_from("<Q", data, i)[0]) & 0xFFFFFFFFFFFFFFFF
        out["banks"].append({
            "base": bank,
            "census": census,
            "total_typed": sum(census.values()),
            "byte_sum": byte_sum,
        })
    # human ship: slot 1 in Campaign, slot 0 in Battle
    slot = 0 if out["mode"] == 2 else 1
    idx = g.u16(OBJ + 0x36C8 + 2 * slot)
    ship = {"slot": slot, "index": idx}
    for b, bank in enumerate(banks):
        if not bank:
            continue
        rec = bank + idx * 256
        ship[f"bank{b}"] = {
            "type": g.read(rec + 0x0F, 1)[0],
            "health": g.f32(rec + 0x50),
            "max": g.f32(rec + 0x64),
            "pos": [g.f32(rec + 0), g.f32(rec + 4), g.f32(rec + 8)],
        }
    out["ship"] = ship
    print(json.dumps(out, indent=1, sort_keys=True))


if __name__ == "__main__":
    main()
