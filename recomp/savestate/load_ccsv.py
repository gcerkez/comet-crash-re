#!/usr/bin/env python3
"""External stop-the-world restore of a CCSV0001 file into a running
process: SIGSTOP the target, reconcile + write pages via /proc/<pid>/mem,
SIGCONT. Phase-1-style load mechanism, v2 file format. Diagnostic use."""
import os, signal, struct, sys

pid = int(sys.argv[1]); path = sys.argv[2]
with open(path, "rb") as f:
    blob = f.read()
magic = blob[0:8]
(ver, hdr, mission, mode, state) = struct.unpack_from("<IIIII", blob, 8)
(ts,) = struct.unpack_from("<Q", blob, 32)
(count,) = struct.unpack_from("<I", blob, 40)
(span,) = struct.unpack_from("<Q", blob, 48)
assert magic.rstrip(b"\0") == b"CCSV0001", magic
assert hdr == 64, hdr
print(f"file: {count} pages, span 0x{span:x}, mission {mission} mode {mode} state {state}")
off = hdr
pages = []
for _ in range(count):
    (addr,) = struct.unpack_from("<I", blob, off); off += 4
    pages.append((addr, blob[off:off+4096])); off += 4096
assert off == len(blob), (off, len(blob))
in_save = set(a for a, _ in pages)

maps = open(f"/proc/{pid}/maps").read().splitlines()
vm = None
for ln in maps:
    parts = ln.split()
    lo, hi = (int(x, 16) for x in parts[0].split("-"))
    if hi - lo == 0x100010000:
        vm = lo; break
assert vm, "guest mapping not found"
print(f"vm_base=0x{vm:x}")

os.kill(pid, signal.SIGSTOP)
import time
for _ in range(3000):
    st = open(f"/proc/{pid}/stat").read()
    if st[st.rfind(")")+2] == "T": break
    time.sleep(0.001)
else:
    print("WARN: target did not report T state")
mem = os.open(f"/proc/{pid}/mem", os.O_RDWR)
zero = bytes(4096)
nz = 0
for addr in range(0, span, 4096):
    if addr in in_save: continue
    cur = os.pread(mem, 4096, vm + addr)
    if cur != zero:
        os.pwrite(mem, zero, vm + addr); nz += 1
w = 0
for addr, data in pages:
    os.pwrite(mem, data, vm + addr); w += 1
os.close(mem)
os.kill(pid, signal.SIGCONT)
print(f"restored: {w} pages written, {nz} zero-filled; SIGCONT sent")
