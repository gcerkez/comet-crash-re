#!/usr/bin/env python3
"""patch_fork_fifo_resync_scan.py -- SAVESTATE FIFO RESYNC,
scan back to the last flip marker (fork, cellGcmSys.c).

Refinement of the flip-completion half: instead of only
accounting the saved session's last flip, position the
walker AT that flip's marker, so the drain executes it in
order -- executing the marker is what sets the display
buffer, raises the flip-pending flag the present loop waits
on, and counts the reach. The scan runs backward from the
restored current over at most GCM_FLIP_WRAP_BYTES (the
fork's own bound for a frame's command volume) inside the
restored ring, and mirrors the walker's marker test. If no
marker is in the window the resync keeps the restored
current and the queued/reached accounting from the previous
step still applies. The drained tail is bounded, starts on
a true command boundary, and is the saved session's own
stream -- the state it re-issues agrees with the restored
guest by construction.

Applies on top of patch_fork_fifo_resync.py. Anchor
asserted; idempotent via its own marker.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "tools/ps3recomp"
p = root / "libs/video/cellGcmSys.c"
t = p.read_text()
if "scan back to the last flip marker" in t:
    print(f"{p}: already patched")
    sys.exit(0)

old = """    fprintf(stderr, "[cellGcmSys] savestate load: FIFO walker "
            "0x%08X -> 0x%08X (restored guest position)\\n",
            s_fifo_getoff, target);"""
new = """    /* SAVESTATE FIFO RESYNC (scan back to the last flip marker):
     * if the saved session's last flip is still unreached, its
     * marker sits in the tail behind the restored current.
     * Start the walker AT the marker so the drain executes it
     * in order -- executing it is what notes the display
     * buffer, raises the flip-pending flag the present loop
     * waits on, and counts the reach. The scan is bounded by
     * GCM_FLIP_WRAP_BYTES (the fork's own bound for a frame's
     * command volume) and mirrors the walker's marker test;
     * with no marker in the window the restored current
     * stands. */
    if (s_gcm_ctx_out_ea) {
        const u32 ctx = vm_read32(s_gcm_ctx_out_ea);
        const u32 begin_ea = ctx ? vm_read32(ctx + 0x0) : 0;
        const u32 end_ea   = ctx ? vm_read32(ctx + 0x4) : 0;
        const u32 io_begin = begin_ea ? gcm_ea2io(begin_ea) : 0;
        const u32 io_end   = end_ea ? gcm_ea2io(end_ea) : 0;
        if (io_begin && io_end > io_begin) {
            const u32 ring = io_end - io_begin;
            u32 scan = GCM_FLIP_WRAP_BYTES;
            if (scan > ring) scan = ring;
            for (u32 back = 4; back <= scan; back += 4) {
                u32 off = target - back;
                if (off < io_begin) off += ring;
                const u32 ea = gcm_io2ea(off);
                if (!ea) continue;
                if ((vm_read32(ea) & 0xFFFFFF00u) == GCM_FLIP_MARKER) {
                    target = off;
                    break;
                }
            }
        }
    }
    fprintf(stderr, "[cellGcmSys] savestate load: FIFO walker "
            "0x%08X -> 0x%08X (restored guest position)\\n",
            s_fifo_getoff, target);"""
assert t.count(old) == 1, "resync log anchor not unique/found"
t = t.replace(old, new, 1)
p.write_text(t)
print(f"{p}: scan-back to last flip marker applied")
