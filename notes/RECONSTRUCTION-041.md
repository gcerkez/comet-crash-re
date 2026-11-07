# 041 — Cheats: investigation, Thorium pin, stage-1 host effects

Jerry's request (2026-11-07): a "Cheats" entry on the Options menu with,
at minimum, an Infinite Thorium toggle (all modes) and an All Buildables
toggle (every model type, including ones a level disables), following the
game's own UI standards; plus additional debug-level options. Gemini
(Antigravity, Flash) produced a research report; every load-bearing claim
below was cross-checked against the corpus/lift by Muse before use.

## No cheats exist in the shipped game

Full 265-string table, the whole input chain (pads_update → pads_sample →
FUN_0010d390), and the settings menus were swept: no cheat/debug strings,
no multi-button sequence tests, no hidden toggles. The "Sulpha" DECI3
strings are inert Sony devkit debugger middleware. The retail custom map
editor (strings 254–264) is the only legitimate high-Thorium route.

## Thorium balance — pinned empirically (live session, no-freeze reads)

Resident guest RAM was scanned/diffed through /proc/<pid>/mem + pagemap
(no ptrace attach, no game freeze) across a controlled spend in an
Expansion L1 match (start 430; one 40-cost build):

- The balance lives in TWO lockstep records inside the central
  game-state object (guest 0x30E600): **+0x1B4 and +0x1D4**, both moving
  430 → 390 with the build, matching the on-screen value exactly.
- Record shape (8 words from +0x1B0): [0x7FFFFFFF][balance][100000 ×6].
  arena_enterInit (decompiled.c:128333–128360) seeds both records
  pairwise-identically from eight float channels at obj+0x348+i*0x80;
  channel 1 (obj+0x3C8) is the balance source. The +0x1D0 record is a
  shadow snapshot of the same stat block — a pin must write both, plus
  the float channel for safety.
- Other 430-valued words in RAM (0x24D8F4, 0x871EF4, 0x9E9650) did not
  move — configs, not the wallet.
- The object at 0x30E600 is the game's central state object: method
  table 0x1F1E28 ([2]=op_dispatch, [3]=camera_zoomInput), installed by
  FUN_000e0cf8/FUN_000e14c0/FUN_000e1e58. ALL corpus DAT_0055xxxx labels
  are byte offsets from this object; its base is a pooled constant
  (0x30E600 in ≥16 TOC slots), i.e. fixed in the recomp.

## Menu mechanics (cross-checked)

- Options is built by FUN_000f8168; items append via FUN_001533d0
  (opcode at item+0x50, stride 0x22C, selected index at menu+0xF0) in
  the order Controls 0x44/0x45, Video Record 0x5A, Music 0x50, Effects
  0x4F, Zoom 0x53, Language 0x4C (conditional), Credits 99.
- op_dispatch (FUN_000f6990) bound is opcode ≤ 100; 101 silently
  returns 0 and 100 is taken (Gemini's proposed 101 was wrong). Free
  no-op opcodes: 14, 30, 31, 37, 47, 49, 57, 67.
- Sliders never reach op_dispatch (owner vtable+0xC callback; volume
  steps of 5). The toggle idiom to copy is Video Record's: submenu
  rebuilt on every open (FUN_000fa0b4) + a flag-flip handler
  (FUN_000f6c24, opcode 0x5B: vm_write8(ptr, b^1)).

## Buildables gate (cross-checked; Gemini partly wrong)

Layers in FUN_0010d390 (decompiled.c:146205–146300):
1. Mode (obj+0x55209C) → threshold: campaign = mission id
   (obj+0x551EBC); mode 1 → 1; modes 2/3 → 1000.
2. Requirement table at guest 0x17F330: 28 records × 20 B, indexed
   (kind−20)*20, first word = required level (kinds 20–28: 23,0,8,2,
   10,3,0,33,5; 31→30; 32→36; units 40→63 "never", 41→61, 42→0,
   43/44→63). Accept iff req ≤ threshold−1.
3. Expansion remap: missions 50–55 → thresholds 7/14/21/28/35/42
   (tables at 0x17F540/0x17F560).
4. Dormant alt-table branch: flag byte at guest 0x873061 (no corpus
   writer, inits 0) would switch the table base to 0x17F434 — the same
   words from record 13, so zeroing all 28 records covers it.
5. Tech mask (u16 at obj+0x302+p*0x80; sole writer FUN_000ec760 from
   arena_enterInit, copying level fields obj+0x56390C/E) gates ONLY
   kind 27 (bit 5) and kind 32 (bit 4), in every mode; mode 3 skips
   the table check but keeps the bits.
Gemini's FUN_0010c228/FUN_0010c8f0 do not exist (fabricated). Unit sends
run through FUN_001637b8 against a runtime table at guest 0x8B32F4 and
stockpiles at obj+0x364+p*0x80+k*2 — not the mask.

Unlock-all recipe: zero all 28 req words + set mask bits 4/5.

## FPS overlay — real gate, unusable as a simple toggle

debug_fpsOverlay (FUN_00101258) is gated on the byte at guest 0x2BBFB4
(TOC-held absolute pointer; gate at decompiled.c:140542; single call
site 0xE46FC). BUT the byte ships as 1 in the ELF, game_loop toggles it
per frame from frame-time conditions (decompiled.c:125982–125990), and
it gates a family of other debug draws (7+ more read sites). Dropped
from the toggle list.

## Stage 1 — implemented in the glue (tools main.cpp; repo copy synced)

`comet_cheats_tick()`, called from comet_crash_camera_tick, applies
three host-side toggles (state also exposed to stage 2 via
comet_cheats_get/comet_cheats_flip):

- **thorium**: in states 13/14, when both shadow records carry the
  0x7FFFFFFF sentinel, pin obj+0x1B4 = obj+0x1D4 = 100000 and the float
  channel obj+0x3C8 = 100000.0f, every frame.
- **buildables**: edge-triggered — snapshot and zero the 28 req words
  on enable, restore on disable; while on and in a match, set mask
  bits 4/5 for all four player slots every frame.
- **stockpiles**: in states 13/14, pin obj+0x364+p*0x80+k*2 = 999
  (p 0–1, k 0–7) every frame.

Control: env CC_CHEAT_THORIUM / CC_CHEAT_BUILDABLES /
CC_CHEAT_STOCKPILES at launch, and/or a live control file
(CC_CHEATS_FILE, default ~/.cc_cheats, lines "thorium=1" etc.) re-read
on mtime change every 30 frames. State changes log "[cheats] …".

## Stage-1 verification (driven headless, 2026-11-07) — PASSED

Two driven sessions (Xvfb + PAD_FILE, Campaign → Kreios Area 1, all
three toggles on via env), with live readback/poke through
/proc/<pid>/mem:

- Boot log: "[cheats] env: thorium=1 buildables=1 stockpiles=1" then
  "[cheats] requirement table neutralised"; the 28 req words read back
  0 in live memory.
- In the match: wallet records read [0x7FFFFFFF][100000][100000 ×6],
  float channel = 100000.0f, tech mask p0 = 0x30 (bits 4+5 set),
  stockpiles = 999 (u16s).
- Poke test (decisive): writing 50000 into both wallet records during
  live play was restored to 100000 within 2 s. With toggles flipped
  off via ~/.cc_cheats, the same poke persisted, the log showed the
  file change + "requirement table restored", and req word 0 read
  back its original 23.
- State-word caveat: obj+0x5520C0 sampled externally (slow /proc
  polls) reads 0 almost always, even mid-match — it only shows 13/14
  during the guest's active frame-processing windows. The glue's
  per-flip sampling catches it reliably (pins applied throughout a
  live match, never at menus — the menu wallet kept its static 1000 —
  and never on the battle-end screen, where a poke persisted).
  Do not chase this as a bug.
- Not visually confirmed: the R1 palette contents under buildables
  (palette did not render in the overview-driven shots). The gate's
  two inputs (table + mask) were verified in memory; a controller
  play-test will show the palette directly.

## Remaining
- Stage 2: the guest Cheats submenu — assert-anchored patch to the
  lifted code (Options builder append after Credits with a free
  opcode; dispatch cases building a Video-Record-style submenu whose
  rows flip the glue state), applied post-lift by setup.py so it
  survives re-lifts; labels planted by the glue (no "Cheats" string
  exists in the game's table).
- The runtime ADD/SPEND writers of the wallet were not statically
  identifiable; a data watchpoint in a coordinated session could close
  that (not needed for the pin).
