#!/usr/bin/env python3
"""patch_fork_fifo_resync_scan3.py -- SAVESTATE FIFO RESYNC,
back to the last flip marker (fork, cellGcmSys.c).

The previous-frame-boundary start (scan2) was falsified in
validation: it produced MORE poisoned pipelines than the
last-marker start (2 vs 1), because the register state a
frame's draws inherit is set by stream history that predates
the frame -- replaying the whole frame cannot re-issue it,
it only executes more draws under mixed provenance. (The
residual boundary failures under either start are draws
whose shader-program address was set before the window and
whose local-memory contents differ between the two sessions;
they are confined to the first frame after the load and the
guest's own state stream re-covers every material as it
plays.) Revert the scan to the first marker found -- the
saved session's last flip -- keeping the two-frame scan
window (harmless: the loop stops at the first marker).

Applies on top of patch_fork_fifo_resync_scan2.py. Anchor
asserted; idempotent via its own marker.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "tools/ps3recomp"
p = root / "libs/video/cellGcmSys.c"
t = p.read_text()
if "first marker found stands" in t:
    print(f"{p}: already patched")
    sys.exit(0)

old = """            u32 found = 0;
            int markers = 0;
            for (u32 back = 4; back <= scan; back += 4) {
                u32 off = target - back;
                if (off < io_begin) off += ring;
                const u32 ea = gcm_io2ea(off);
                if (!ea) continue;
                if ((vm_read32(ea) & 0xFFFFFF00u) == GCM_FLIP_MARKER) {
                    found = off;
                    if (++markers == 2) break;
                }
            }
            if (markers) target = found;"""
new = """            /* First marker found stands: it is the saved
             * session's last flip. (Starting a frame earlier
             * was tried and was worse -- the register state a
             * frame's draws inherit predates the frame, so
             * replaying it executes more draws under mixed
             * provenance, not fewer.) */
            for (u32 back = 4; back <= scan; back += 4) {
                u32 off = target - back;
                if (off < io_begin) off += ring;
                const u32 ea = gcm_io2ea(off);
                if (!ea) continue;
                if ((vm_read32(ea) & 0xFFFFFF00u) == GCM_FLIP_MARKER) {
                    target = off;
                    break;
                }
            }"""
assert t.count(old) == 1, "scan2 loop not unique/found"
t = t.replace(old, new, 1)

old_comment = """    /* SAVESTATE FIFO RESYNC (scan back to the previous frame
     * boundary): if the saved session's last flip is still
     * unreached, its marker sits in the tail behind the
     * restored current. Start the walker at the marker BEFORE
     * it -- the start of the frame the saved session had in
     * flight -- so the drain re-issues that frame's state
     * setup and draws in order and executes its flip at the
     * end (executing it is what notes the display buffer,
     * raises the flip-pending flag the present loop waits on,
     * and counts the reach). Starting at the last marker
     * instead left the frame's first draw running on the
     * pre-load session's program registers, which fetched
     * payload data as a fragment program (one poisoned
     * pipeline in validation). The scan covers two frames'
     * command volume (2x GCM_FLIP_WRAP_BYTES) and mirrors the
     * walker's marker test; with fewer markers in the window
     * it falls back to the last marker, then to the restored
     * current. */"""
new_comment = """    /* SAVESTATE FIFO RESYNC (scan back to the last flip
     * marker): if the saved session's last flip is still
     * unreached, its marker sits in the tail behind the
     * restored current. Start the walker AT the marker so the
     * drain executes it in order -- executing it is what
     * notes the display buffer, raises the flip-pending flag
     * the present loop waits on, and counts the reach. The
     * scan covers two frames' command volume (2x
     * GCM_FLIP_WRAP_BYTES) and mirrors the walker's marker
     * test; with no marker in the window the restored current
     * stands. */"""
assert t.count(old_comment) == 1, "scan2 comment not unique/found"
t = t.replace(old_comment, new_comment, 1)

p.write_text(t)
print(f"{p}: scan reverted to last flip marker")
