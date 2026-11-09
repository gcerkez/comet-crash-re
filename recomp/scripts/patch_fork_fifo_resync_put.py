#!/usr/bin/env python3
"""patch_fork_fifo_resync_put.py -- SAVESTATE FIFO RESYNC,
keep the guest's put when scan-back moves the walker (fork,
cellGcmSys.c).

Correction to the scan-back step: it moved `target` back to
the flip marker, and the resync then published target into
the control register's put -- overwriting the guest's own
put (the restored current) with the marker position. The
walker chases the control put, so it now believed itself
caught up AT the marker and never consumed the tail at all.
Split the two values: the walker position (s_fifo_getoff and
the get mirror) takes the scan-back target, but the control
put is published from the restored guest position captured
before the scan.

Applies on top of patch_fork_fifo_resync_scan.py. Anchors
asserted; idempotent via its own marker.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "tools/ps3recomp"
p = root / "libs/video/cellGcmSys.c"
t = p.read_text()
if "guest_put" in t:
    print(f"{p}: already patched")
    sys.exit(0)

edits = [
    # 1. capture the guest position before the scan moves target
    ("""    /* SAVESTATE FIFO RESYNC (scan back to the last flip marker):""",
     """    /* The guest's own put is the restored position BEFORE any
     * scan-back: the control register keeps publishing it (the
     * walker chases it), even when the walker itself starts
     * behind it at the flip marker. */
    const u32 guest_put = target;
    /* SAVESTATE FIFO RESYNC (scan back to the last flip marker):"""),
    # 2. publish guest_put, not the (possibly scanned-back) target
    ("""    vm_write32(GCM_CONTROL_GUEST_ADDR + 0, target);
    vm_write32(GCM_CONTROL_GUEST_ADDR + 4, target);""",
     """    vm_write32(GCM_CONTROL_GUEST_ADDR + 0, guest_put);
    vm_write32(GCM_CONTROL_GUEST_ADDR + 4, target);"""),
]
for old, new in edits:
    assert t.count(old) == 1, f"anchor not unique/found: {old[:50]!r}"
    t = t.replace(old, new, 1)

p.write_text(t)
print(f"{p}: guest put preserved across scan-back")
