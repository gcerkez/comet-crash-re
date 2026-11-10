#!/usr/bin/env python3
"""Savestate feasibility prototype: pause-boundary guest-memory snapshot.

Subcommands:
  sample <pid>                 print JSON observables of the live match
  save   <pid> <file.ccsave>   SIGSTOP, snapshot guest 0..0x20000000 (non-zero pages), SIGCONT
  load   <pid> <file.ccsave>   SIGSTOP, restore pages + zero-fill the rest of the span, SIGCONT
"""
import json, os, signal, struct, sys, time

OBJ = 0x30E600
SPAN = 0x20000000
PAGE = 4096
MAGIC = b"CCSAVE01"

def vm_base(pid):
    with open("/proc/%d/maps" % pid) as f:
        for line in f:
            parts = line.split()
            lo, hi = (int(x, 16) for x in parts[0].split("-"))
            if hi - lo == 0x100010000 and "rw" in parts[1]:
                return lo
    raise SystemExit("guest mapping (0x100010000 rw) not found for pid %d" % pid)

class Guest:
    def __init__(self, pid):
        self.pid = pid
        self.base = vm_base(pid)
        self.fd = os.open("/proc/%d/mem" % pid, os.O_RDWR)
    def read(self, gaddr, n):
        return os.pread(self.fd, n, self.base + gaddr)
    def write(self, gaddr, data):
        return os.pwrite(self.fd, data, self.base + gaddr)
    def u32(self, a): return struct.unpack(">I", self.read(a, 4))[0]
    def u16(self, a): return struct.unpack(">H", self.read(a, 2))[0]
    def f32(self, a): return struct.unpack(">f", self.read(a, 4))[0]

def sample(pid):
    g = Guest(pid)
    s = {}
    s["mission"] = g.u32(OBJ + 0x551EBC)
    s["mode"] = g.u32(OBJ + 0x55209C)
    s["state"] = g.u32(OBJ + 0x5520C0)
    s["thorium_slot1"] = g.f32(OBJ + 0x3C8)
    s["ship_x"] = g.f32(OBJ + 0x552070)
    s["ship_z"] = g.f32(OBJ + 0x552070 + 8)
    bankA = g.u32(OBJ + 0x180)
    bankB = g.u32(OBJ + 0x198)
    s["bankA"] = "0x%08x" % bankA
    s["bankB"] = "0x%08x" % bankB
    idx = g.u16(OBJ + 0x36C8 + 2 * 1)
    s["ship_index"] = idx
    if bankA:
        rec = bankA + idx * 256
        s["ship_type_bankA"] = g.read(rec + 0x0F, 1)[0]
        s["ship_health_bankA"] = g.f32(rec + 0x50)
        s["ship_max_bankA"] = g.f32(rec + 0x64)
        data = g.read(bankA, 1024 * 256)
        s["census_bankA_1024"] = sum(1 for i in range(1024) if data[i * 256 + 0x0F] != 0)
    if bankB:
        recB = bankB + idx * 256
        s["ship_health_bankB"] = g.f32(recB + 0x50)
    return s

def cmd_save(pid, path):
    t0 = time.time()
    os.kill(pid, signal.SIGSTOP)
    try:
        g = Guest(pid)
        hdr = {
            "format": "ccsave-prototype", "version": 1,
            "mission": g.u32(OBJ + 0x551EBC),
            "mode": g.u32(OBJ + 0x55209C),
            "state": g.u32(OBJ + 0x5520C0),
            "wall_time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "guest_span": SPAN, "page_size": PAGE,
        }
        pages = []
        CH = 1 << 20
        for off in range(0, SPAN, CH):
            chunk = g.read(off, CH)
            if not chunk.strip(b"\0"):
                continue
            for p in range(CH // PAGE):
                page = chunk[p * PAGE:(p + 1) * PAGE]
                if page.strip(b"\0"):
                    pages.append((off // PAGE + p, page))
        hdr["page_count"] = len(pages)
        hdr["total_bytes"] = len(pages) * PAGE
        hb = json.dumps(hdr).encode()
        with open(path, "wb") as f:
            f.write(MAGIC)
            f.write(struct.pack("<I", len(hb)))
            f.write(hb)
            for idx, page in pages:
                f.write(struct.pack("<I", idx))
                f.write(page)
    finally:
        os.kill(pid, signal.SIGCONT)
    print(json.dumps({"saved": path, "file_size": os.path.getsize(path),
                      "save_seconds": round(time.time() - t0, 2), "header": hdr}))

def cmd_load(pid, path):
    t0 = time.time()
    stored = {}
    with open(path, "rb") as f:
        if f.read(8) != MAGIC:
            raise SystemExit("bad magic")
        hlen = struct.unpack("<I", f.read(4))[0]
        hdr = json.loads(f.read(hlen))
        while True:
            ib = f.read(4)
            if not ib:
                break
            idx = struct.unpack("<I", ib)[0]
            stored[idx] = f.read(PAGE)
    os.kill(pid, signal.SIGSTOP)
    try:
        g = Guest(pid)
        for idx, page in stored.items():
            g.write(idx * PAGE, page)
        zeroed = 0
        CH = 1 << 20
        zeros = b"\0" * CH
        for off in range(0, SPAN, CH):
            chunk = g.read(off, CH)
            if not chunk.strip(b"\0"):
                continue
            for p in range(CH // PAGE):
                gi = off // PAGE + p
                if gi in stored:
                    continue
                if chunk[p * PAGE:(p + 1) * PAGE].strip(b"\0"):
                    g.write(gi * PAGE, zeros[:PAGE])
                    zeroed += 1
    finally:
        os.kill(pid, signal.SIGCONT)
    print(json.dumps({"loaded": path, "pages_written": len(stored),
                      "pages_zero_filled": zeroed,
                      "load_seconds": round(time.time() - t0, 2), "header": hdr}))

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "sample":
        print(json.dumps(sample(int(sys.argv[2])), indent=1))
    elif cmd == "save":
        cmd_save(int(sys.argv[2]), sys.argv[3])
    elif cmd == "load":
        cmd_load(int(sys.argv[2]), sys.argv[3])
    else:
        raise SystemExit("usage: svtools.py sample|save|load ...")
