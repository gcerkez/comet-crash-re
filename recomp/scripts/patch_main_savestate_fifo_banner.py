#!/usr/bin/env python3
"""patch_main_savestate_fifo_banner.py -- fold the round-4 banner
paragraph into a workspace main.cpp whose module banner predates
it (patch_main_savestate.py's module already carries it, so fresh
applications do not need this). Keeps an already-patched
workspace's module byte-identical to the patch script's module.
Idempotent via its own marker sentence.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "tools/cc-recomp"
p = root / "main.cpp"
t = p.read_text()
if "Round 4 (SAVESTATE FIFO RESYNC): a real-user load" in t:
    print(f"{p}: banner already folded")
    sys.exit(0)

old = """ * is not reset either; it re-reads its guest ring state
 * every block.
 * -----------------------------------------------------------------------*/"""
new = """ * is not reset either; it re-reads its guest ring state
 * every block.
 *
 * Round 4 (SAVESTATE FIFO RESYNC): a real-user load on the real
 * GPU restored the match but killed the picture -- fragment
 * programs that translated fine pre-load failed HLSL
 * translation after it ('array index out of range'), and a
 * second attempt desynced the guest FIFO outright (unmapped
 * JUMP + resync). Root cause: the host FIFO walker's position
 * (fork-side s_fifo_getoff) is host-private state from the
 * PRE-load session and nothing re-derived it from the restored
 * guest state, so on resume the walker drained the restored
 * ring from its stale offset -- replaying consumed history,
 * parsing inline vertex/uniform payload as method headers
 * (garbage state: absurd blits, poisoned shader/texture state,
 * fragment programs decompiled from the wrong bytes), or
 * hitting an unmapped JUMP. The fix is a walker hold + resync,
 * not another cache flush: ps3_gcm_fifo_savestate_hold parks
 * the walker with the pre-stop resyncs, and
 * ps3_gcm_fifo_savestate_resync -- after the copy, on every
 * outcome -- moves it to the restored guest context's current
 * position and clears its pass state. Only host-private shadow
 * state moves; guest-visible caches stay exactly as restored.
 * -----------------------------------------------------------------------*/"""
assert t.count(old) == 1, "banner anchor not unique/found"
p.write_text(t.replace(old, new, 1))
print(f"{p}: round-4 banner folded")
