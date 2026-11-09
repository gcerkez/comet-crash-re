#!/usr/bin/env python3
"""patch_main_savestate_fifo.py -- SAVESTATE FIFO RESYNC (glue).

Incremental edit for a workspace main.cpp that already carries
the SAVESTATE PATCH module (round 4, notes 051). Wires the fork's
FIFO-walker resync exports into ss_do_load:

  - weak declarations for ps3_gcm_fifo_savestate_hold /
    ps3_gcm_fifo_savestate_resync next to the pad/codec ones;
  - the HOLD fires with the other pre-stop resyncs (after total
    validation, before the stop-the-world copy): while held, the
    fork's drain entry returns immediately, so no drain can
    consume restored-ring bytes from the pre-load walker position
    in the window between SIGCONT and the resync;
  - the RESYNC fires after the restore on both platform paths:
    it moves the host FIFO walker to the restored guest's own
    position (fork-side derivation from the restored context),
    clears the walker's pass state and releases the hold.
    Host-private shadow state only -- no guest-visible cache is
    invalidated (the round-2/3 flush lesson).

The same text is folded into patch_main_savestate.py's module
for fresh applications. Every anchor asserted; idempotent via
the SAVESTATE FIFO RESYNC marker.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "tools/cc-recomp"
p = root / "main.cpp"
t = p.read_text()
if "SAVESTATE FIFO RESYNC" in t:
    print(f"{p}: already patched")
    sys.exit(0)

edits = [
    # 1. weak declarations next to the pad/codec resync decls
    ('extern "C" void ps3_pad_resync_savestate(void) __attribute__((weak));\n'
     'extern "C" void ps3_spu_mp3_reset_streams(void) __attribute__((weak));',
     'extern "C" void ps3_pad_resync_savestate(void) __attribute__((weak));\n'
     'extern "C" void ps3_spu_mp3_reset_streams(void) __attribute__((weak));\n'
     '/* SAVESTATE FIFO RESYNC: the FIFO walker hold + resync\n'
     ' * (round 4, notes 051). */\n'
     'extern "C" void ps3_gcm_fifo_savestate_hold(void) __attribute__((weak));\n'
     'extern "C" void ps3_gcm_fifo_savestate_resync(void) __attribute__((weak));'),
    # 2. hold with the pre-stop resyncs
    ('    if (ps3_pad_resync_savestate) ps3_pad_resync_savestate();\n'
     '    if (ps3_spu_mp3_reset_streams) ps3_spu_mp3_reset_streams();',
     '    if (ps3_pad_resync_savestate) ps3_pad_resync_savestate();\n'
     '    if (ps3_spu_mp3_reset_streams) ps3_spu_mp3_reset_streams();\n'
     '    /* SAVESTATE FIFO RESYNC: park the host FIFO walker before\n'
     '     * the copy and keep it parked until the resync below moves\n'
     '     * it to the restored guest position. Its position is\n'
     '     * host-private state from this session; left alone it\n'
     '     * would drain the restored ring from the wrong offset. */\n'
     '    if (ps3_gcm_fifo_savestate_hold) ps3_gcm_fifo_savestate_hold();'),
    # 3. resync after the stop-the-world restore (POSIX path)
    ('    const int rc = ss_restore_stop_the_world(path, blob, &h, in_save, t0);\n'
     '    free(in_save);',
     '    const int rc = ss_restore_stop_the_world(path, blob, &h, in_save, t0);\n'
     '    /* SAVESTATE FIFO RESYNC: memory is the saved session\'s\n'
     '     * again; move the walker to match before it drains\n'
     '     * anything (fires on every restore outcome -- on a\n'
     '     * pre-write failure the memory is still this session\'s\n'
     '     * and the resync is a position no-op). */\n'
     '    if (ps3_gcm_fifo_savestate_resync) ps3_gcm_fifo_savestate_resync();\n'
     '    free(in_save);'),
    # 4. resync after the live copy (Windows path)
    ('        memcpy(vm_base + addr, rec + sizeof addr, kSsPage);\n'
     '    }\n'
     '    free(in_save);',
     '        memcpy(vm_base + addr, rec + sizeof addr, kSsPage);\n'
     '    }\n'
     '    /* SAVESTATE FIFO RESYNC: as the POSIX path. */\n'
     '    if (ps3_gcm_fifo_savestate_resync) ps3_gcm_fifo_savestate_resync();\n'
     '    free(in_save);'),
]
for old, new in edits:
    assert t.count(old) == 1, f"anchor not unique/found: {old[:60]!r} count={t.count(old)}"
    t = t.replace(old, new, 1)

p.write_text(t)
print(f"{p}: SAVESTATE FIFO RESYNC (glue) applied")
