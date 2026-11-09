# 051 — Save states (save/load a mid-match level): Phase 1 feasibility

Branch: `muse/save-states`. Prototype tooling: `recomp/savestate/`.
All tests with the `build-batch` binary under Xvfb (llvmpipe), Campaign
Kreios Area 1, Jerry's `~/.cc_cheats` active (Thorium pinned at 1000 —
which is why Thorium is useless as a state observable below).

## Step 0 — does upstream already have this?

No. Grepping the ecosystem clones (upstream, soulbrandt-runtime,
haze-runtime) and the fork for savestate/save_state finds only:
`YDKJ_SPU_LSDUMP` (a diagnostic that compares SPU LS against an RPCS3
savestate), `s_rtsave_state` (D3D12 render-target bookkeeping), and
"checkpoint" in the milestone/SPU-lockstep sense. There is no
save/restore-state feature anywhere in the toolkit to adapt.

## Design under test

External, pause-boundary snapshot of guest RAM:

- Save/load only while the guest state word (obj+0x5520C0) == 14
  (pause). The pause menu's own structures are inside the snapshot;
  on restore the game wakes in the pause menu of the restored match.
- `svtools.py save` SIGSTOPs the process, reads guest
  0x00000000..0x20000000 via /proc/<pid>/mem (vm_base = start of the
  0x100010000 rw mapping), stores only non-zero 4 KiB pages plus a
  JSON header (magic, format version, mission id, mode, state,
  wall-clock time, page count). `load` SIGSTOPs, writes the stored
  pages back, and zero-fills any target page that is non-zero but
  absent from the save (full-span reconciliation). `sample` reads
  the observables: mission (obj+0x551EBC), mode (obj+0x55209C),
  state, Thorium float (obj+0x3C8), ship camera-global
  (obj+0x552070), ship health via the entity-bank registry
  (banks at obj+0x180/+0x198, index u16 at obj+0x36C8+2*slot,
  health float at record +0x50). `xsample.py` adds CRC32 of both
  entity banks (first 1024 records), a live-entity census (type
  byte not in {0, 63}; 63 = tombstone), and the ship record's own
  position floats.

### Driving gotcha (cost one instance)

`PAD_STICK` in this fork build is an environment **value**
(`"lx,ly,rx,ry[,from,until]"`, sscanf'd once in cellPad.c), not a
file path. Driven flight therefore bakes the stick into the launch
environment (`SS_STICK="0,0,128,128,175,100000"`: menus run centred,
the ship flies up-left from pad-clock 175 s for the rest of the
session). `fly.sh` in the prototype dir is the earlier file-based
attempt — it does not work on this build and is kept only for the
record.

## The save (S1)

Driven into Kreios Area 1 (mission 1, Campaign), ship flown into
the enemy corner and shot down en route, paused, saved to
`~/cc-savestates/test1.ccsave`:

- File: **114,234,426 bytes; 27,862 non-zero pages of 131,072;
  save time 1.0 s** (SIGSTOP + 512 MiB scan + write). Header reads
  mission 1, mode 0, state 14.
- S1 observables: state 14; ship health **688.0**/1000 in BOTH
  banks; ship record pos (0.240, 0.240) (corner-clamped); camera
  global (5.186, 11.153); live census **157**; bank CRC32
  A=**f59bf155**, B=**ab1187a6**; base life 25/25.
- Paused guest RAM is **static**: two samples 6 s apart are
  identical on every observable including both CRCs. That is what
  makes pause the right snapshot boundary.
- Determinism note: instance 2, flown with the identical stick
  script, reproduced the ship damage **exactly** (688.0 again).
  The sim is deterministic under identical input, so a restored
  match replays faithfully.

Divergence control (same process, unpause, ~75 s more play,
pause): CRCs move to e4bdd85b/0f8a393f, census 157 -> 161, HUD
base life drops 25 -> 19, timer 03:21. State really moved.

## Leg 3 — in-place load (same process, later timeline): RAM perfect, runtime desync

Loading S1 back into the same process (paused at the diverged
state) restores guest RAM **exactly**: post-load sample == S1 on
every observable, and a full-page audit minutes later finds only
**5 of 27,862 pages** differing from the file (4 low-memory
execution pages, 1 at 0x11113000); base life reads 25 (S1), not
19. But the restored pause is a diorama:

- The on-screen pause backdrop/HUD still shows the **later**
  state (life 19, timer 03:21, the enemy swarm). In pause the
  game composites the menu over the last presented gameplay
  surface; that surface lives in the RSX/backend surface cache,
  which a guest-RAM snapshot does not capture.
- **Input is dead.** The log's `[cellPad] PAD_FILE press`
  admissions stop at 9 — exactly the pre-load presses. START,
  CROSS, and a long DOWN hold after the load are never admitted
  (admission lives in `cellPadGetData`, cellPad.c:1093/1289), and
  the pause screenshot is byte-identical across attempts.
  Rendering and SPURS cycling continue.
- Diagnosis: the host runtime's state (pad backend statics,
  SPURS bookkeeping) had advanced **past** the snapshot into the
  later timeline; rolling guest RAM back underneath it wedges
  the pad seam. An external restore into a "future" process is
  not sound.

## Leg 4 — fresh process, same mission entered by its own flow: WORKS

Fresh boot, driven into Kreios Area 1 the normal way (its fresh
paused state: census 150, CRCs eb128f3e/fe765201), paused, S1
loaded (1.03 s, 4 pages zero-filled):

- Post-load sample == **S1 identical** (all observables, CRCs
  included).
- START **is** admitted (admissions 7 -> 8) and the match
  **resumes**: state 14 -> 13.
- ~55 s of resumed play: census 157 -> **162**, bank CRCs evolve
  (f19ea6a3/e9046364), camera global returns to exactly S1's
  (5.186, 11.153), frames advance, no crash.
- Audio is alive after this restore: the resumed match's capture
  peaks 0.24–0.41 (vs exact 0.0000 in the wedged windows of leg
  1's session — see below).
- Caveat: X11 screenshots on that display returned black frames
  (GetImage vs the Vulkan present path), so this leg has no
  visual capture; the state evidence above is complete without
  it.

## Leg 5 — load at the title/loading state: RAM transplants, sim wedges

Fresh boot left at the front end (state 11, boot level 152
resident), S1 loaded directly (436 pages zero-filled):

- Post-load sample == S1 exactly (state 14, mission 1, CRCs
  f59bf155/ab1187a6). START admitted (0 -> 1), state -> 13,
  process alive, frames present.
- But the simulation does not run: census frozen at 157, bank A
  CRC changes once (f59bf155 -> c9d126a7) then is static for
  25+ s, bank B never touched. This process's SPURS job-chain
  state belongs to the title scene; the transplanted arena is
  rendered but not simulated.

## Audio caveat (leg 1 session only)

Instance 1's mix went silent between ~405–420 s of its capture —
**during ordinary leg-2 gameplay, ~60–75 s before the load**
(peaks 0.32–0.39 before, exact 0.0000 after, including all
post-load windows). The load did not cause it; it matches the
known driven-session audio-wedge family (notes 045). Instance
2's audio stayed healthy through save-era play, load, and
resume, so "restore kills audio" is not supported — but an
in-glue restore must still reset the host MP3 codec's stream
state (streams are keyed by workspace pointer and a same-format
restart is never detected; notes 040), or music/SFX can wedge
on exactly that seam.

## Verdict

**The pause-boundary guest-RAM snapshot is viable as the state
format** — byte-exact, ~1 s each way, deterministic — **but the
restore is only sound into a process whose host runtime agrees
with the snapshot.** Ranked flows:

1. **In-glue save/load (the product path).** The runtime takes
   the snapshot itself at the pause boundary, where it can
   quiesce and re-sync its own subsystems on restore: pad
   backend statics, SPURS task/queue bookkeeping, RSX surface
   cache (invalidate, so the pause backdrop re-renders from
   restored RAM), and the host MP3 codec's stream table. Every
   failure above is a host-state desync of a kind only the
   runtime itself can repair.
2. External load into a fresh process that has entered the
   same mission and paused (leg 4) works end-to-end today and
   is the fallback UX ("launch, enter the level, pause, Load")
   if in-glue resync proves hard.
3. External in-place load (leg 3) and title-screen load
   (leg 5) do **not** work and should not be shipped as flows.

### What the product implementation must handle (from the prototype)

- **Mission crediting**: the snapshot carries the mission id in
  its header and the full match state in RAM; the game's own
  level-clear path increments campaign progression from guest
  state, so a leg-4-style (or in-glue) resumed match should
  credit mission 1 on clear. **Not yet directly verified** —
  Phase 2 must play a restored match to an actual level clear
  and confirm the campaign records it. This is the requirement
  Jerry called out; it is the first Phase-2 test.
- **Host-state resync list** (in-glue restore duties): pad
  statics, SPURS queues/tasks, RSX surface cache, host codec
  stream table, SDL/audio port state. SPU **local store** is
  not in guest RAM at all — pause works because the sim jobs
  are idle at the boundary; a mid-frame snapshot would not be
  viable, and the product should refuse to save outside pause
  (state != 14).
- **Format**: keep header + sparse pages; add compression
  (114 MB raw is mostly compressible asset data) and consider a
  boot-baseline page exclusion; record fork/build identity in
  the header and refuse mismatched loads; store saves in a
  per-user saves directory, never /tmp.
- **UX surface**: Save/Load rows in the pause menu (next to the
  Cheats rows) or F5/F9-style hotkeys handled by the glue;
  single-slot prototype, multi-slot later. Battle saves (mode
  word 2, per-slot state) are untested — Phase 2.
- Open threads: the 1 differing page at 0x11113000 in the
  leg-3 audit; whether resetting pad statics alone revives a
  leg-3-style restore (would simplify the in-glue design);
  save size after compression.

Prototype scripts: `recomp/savestate/` (svtools.py is the
save/load/sample tool; xsample.py the extended sampler; the
rest are the driven-session harness). The test save itself
(`~/cc-savestates/test1.ccsave` on the desktop) is **not**
committed — it is 114 MB of guest RAM.
