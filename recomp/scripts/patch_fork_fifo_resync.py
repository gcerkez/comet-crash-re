#!/usr/bin/env python3
"""patch_fork_fifo_resync.py -- SAVESTATE FIFO RESYNC (fork, cellGcmSys.c).

Round 4 (notes 051): a savestate restore replaces guest RAM --
including the FIFO ring and the guest's GCM context -- with the
saved session's, but the host FIFO walker's position
(s_fifo_getoff) is host-private state from the PRE-load session
and is never re-derived. On resume the walker drains the restored
ring from its stale offset: it replays already-consumed history,
parses inline vertex/uniform payload as method headers (garbage
blits, poisoned shader/texture state -> fragment programs
decompiled from the wrong bytes fail HLSL translation and the
picture dies), or runs into an unmapped JUMP and resyncs. Two
exports fix it, called by the title glue around the restore:

  - ps3_gcm_fifo_savestate_hold(): park the walker. While held,
    cellGcm_rsx_process_fifo() returns without draining, so no
    drain can slip in between the restore and the resync (the
    load's stop-the-world freezes the guest, not the present
    thread's timing after SIGCONT).
  - ps3_gcm_fifo_savestate_resync(): under the FIFO lock, move
    the walker to the restored guest's own position -- the IO
    offset of the restored context's current (at a pause the
    saved session was drained: get == put == current) -- clear
    the CALL return slot and the park/semaphore pass state,
    publish the control put/get mirrors to match, and release
    the hold. Host-private shadow state only: no surface,
    texture or program cache is touched (the round-2/3 flush
    lesson -- those are guest-visible and dropping them wedges
    the guest).

Every anchor asserted; idempotent via the SAVESTATE FIFO RESYNC
marker. This base script includes the flip-completion
accounting (the resync registers the saved session's last
flip as queued AND reached -- see the export body); the
marker scan-back and its refinements are the follow-on
scripts patch_fork_fifo_resync_scan.py, _put.py, _scan2.py
and _scan3.py, applied in that order.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "tools/ps3recomp"
p = root / "libs/video/cellGcmSys.c"
t = p.read_text()
if "SAVESTATE FIFO RESYNC" in t:
    print(f"{p}: already patched")
    sys.exit(0)

edits = [
    # 1. hold flag next to the walker position
    ("static u32 s_fifo_getoff  = 0;",
     "static u32 s_fifo_getoff  = 0;\n"
     "/* SAVESTATE FIFO RESYNC: while set, cellGcm_rsx_process_fifo()\n"
     " * returns without draining. The title glue holds the walker\n"
     " * across a savestate restore and releases it from\n"
     " * ps3_gcm_fifo_savestate_resync() once the walker position\n"
     " * has been moved to the restored guest's position. */\n"
     "static volatile int s_fifo_ss_hold = 0;"),
    # 2. the hold gate at the public drain entry
    ("void cellGcm_rsx_process_fifo(void)\n"
     "{\n"
     "    wprof_note_walker();",
     "void cellGcm_rsx_process_fifo(void)\n"
     "{\n"
     "    if (s_fifo_ss_hold) return;   /* SAVESTATE FIFO RESYNC */\n"
     "    wprof_note_walker();"),
    # 3. the two exports, after the driver's own FIFO reset
    ("void cellGcm_syscall_set_fifo(u32 put, u32 get)\n"
     "{\n"
     "    vm_write32(GCM_CONTROL_GUEST_ADDR + 0, put);\n"
     "    vm_write32(GCM_CONTROL_GUEST_ADDR + 4, get);\n"
     "    s_fifo_getoff  = get;\n"
     "    s_fifo_calloff = 0;\n"
     "}",
     "void cellGcm_syscall_set_fifo(u32 put, u32 get)\n"
     "{\n"
     "    vm_write32(GCM_CONTROL_GUEST_ADDR + 0, put);\n"
     "    vm_write32(GCM_CONTROL_GUEST_ADDR + 4, get);\n"
     "    s_fifo_getoff  = get;\n"
     "    s_fifo_calloff = 0;\n"
     "}\n"
     "\n"
     "/* SAVESTATE FIFO RESYNC: the title glue holds the FIFO walker\n"
     " * before a savestate restore replaces guest memory, then calls\n"
     " * the resync once the copy is in. The restore rolls the guest's\n"
     " * GCM context and FIFO ring back to the saved session, whose\n"
     " * host walker had drained to the context's current; this\n"
     " * process's walker still sits at the pre-load session's\n"
     " * position and would drain the restored ring from there --\n"
     " * replaying consumed history or parsing payload data as\n"
     " * methods. Move the walker to the restored position instead:\n"
     " * the one cellGcm_syscall_set_fifo moves it for the driver's\n"
     " * own reset, sourced from the restored guest state. Only\n"
     " * host-private shadow state moves; guest-visible caches\n"
     " * (surfaces, textures, programs) are deliberately untouched. */\n"
     "void ps3_gcm_fifo_savestate_hold(void)\n"
     "{\n"
     "    s_fifo_ss_hold = 1;\n"
     "}\n"
     "\n"
     "void ps3_gcm_fifo_savestate_resync(void)\n"
     "{\n"
     "    AcquireSRWLockExclusive(&s_gcm_fifo_lock);\n"
     "    /* The restored guest's position is its own context's\n"
     "     * current (gCellGcmCurrentContext, the pointer cellGcmInit\n"
     "     * handed out -- the same derivation the ring-recycle path\n"
     "     * uses), converted to an IO offset. Fall back to the\n"
     "     * control register's put if the title never took one. */\n"
     "    u32 target = vm_read32(GCM_CONTROL_GUEST_ADDR + 0);\n"
     "    if (s_gcm_ctx_out_ea) {\n"
     "        const u32 ctx = vm_read32(s_gcm_ctx_out_ea);\n"
     "        const u32 cur_ea = ctx ? vm_read32(ctx + 0x8) : 0;\n"
     "        const u32 io = cur_ea ? gcm_ea2io(cur_ea) : 0;\n"
     "        if (io) target = io;\n"
     "    }\n"
     "    fprintf(stderr, \"[cellGcmSys] savestate load: FIFO walker \"\n"
     "            \"0x%08X -> 0x%08X (restored guest position)\\n\",\n"
     "            s_fifo_getoff, target);\n"
     "    s_fifo_getoff  = target;\n"
     "    s_fifo_calloff = 0;\n"
     "    s_last_ring_off = target;\n"
     "    park_off = 0; park_put = 0;\n"
     "    s_sema_blocked_pass = 0; s_park_pass = 0; s_report_hold_pass = 0;\n"
     "    vm_write32(GCM_CONTROL_GUEST_ADDR + 0, target);\n"
     "    vm_write32(GCM_CONTROL_GUEST_ADDR + 4, target);\n"
     "    g_gcm_fifo_drained_ea = gcm_io2ea(target);\n"
     "    /* The restored guest may be waiting on the completion of\n"
     "     * the last flip it queued in the saved session: its marker\n"
     "     * sits behind the restored current, this walker will never\n"
     "     * reach it now, and this session's flip counters never\n"
     "     * counted it -- and a flip that is never reached waits\n"
     "     * forever (the invariant the skip-scan and the\n"
     "     * no-IO-mapping write-off elsewhere in this file state).\n"
     "     * Register it as queued AND reached -- a flip completion\n"
     "     * owed -- so the tick delivers exactly one completion and\n"
     "     * the counters stay consistent for every later flip. If\n"
     "     * the saved flip had in fact completed before the save,\n"
     "     * the pump's sequence stamp absorbs the extra completion\n"
     "     * when no flip is outstanding. */\n"
     "    s_fifo_flips_queued++;\n"
     "    s_fifo_flips_reached++;\n"
     "    s_fifo_ss_hold = 0;\n"
     "    ReleaseSRWLockExclusive(&s_gcm_fifo_lock);\n"
     "}"),
]
for old, new in edits:
    assert old in t, f"anchor missing: {old[:60]}"
    t = t.replace(old, new, 1)

p.write_text(t)
print(f"{p}: SAVESTATE FIFO RESYNC applied")
