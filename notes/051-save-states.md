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

## Phase 2 — in-glue implementation (F5/F9) and the full battery

Phase 1 (above) proved the format with an external /proc tool and
mapped exactly where restores are sound. Phase 2 builds the
product path the verdict calls for — the runtime taking its own
snapshot — and re-tests every leg from inside the glue. The two
phases agree wherever they overlap. The leg-4 flow through the
glue is the one place they differ, and the difference is
instructive: see the drives 8–11 result below.

### What was built

- **Fork** (`muse/save-states`, from d7430289):
  `18ded31d` — `rsx_draw_engine_invalidate_guest_caches()` in the
  draw engine (evict every texture/surface/depth through the
  normal eviction callbacks). `a9ec9227` — the Vulkan backend's
  SDL pump calls new weak hooks `ps3_host_savestate_save()` /
  `ps3_host_savestate_load()` on **F5/F9** (the
  `ps3_host_quit_needs_confirm` pattern), and exports
  `ps3_rsx_flush_guest_caches()` (submit + device idle, drop
  cached framebuffers, engine invalidate, unbind fallback decoded
  units). This is the RSX surface-cache duty from Phase 1's
  resync list, so a restored pause re-renders its backdrop from
  restored RAM instead of compositing the later timeline's
  surface (Phase 1 leg 3's stale backdrop).
- **Glue** (`recomp/scripts/patch_main_savestate.py` applied to
  the workspace `main.cpp`): the capture/restore module — sparse
  nonzero-page snapshot of guest [0, 0x40000000) (mincore-bounded),
  header `CCSV0001` v1 {mission id, mode word, state word, unix
  time, page count, restore span}, total file validation before
  any guest byte changes (magic/version/exact size/record
  alignment+span), zero-the-span then copy-back restore, cache
  flush via the backend hook — plus a debug trigger file
  (`saves/trigger`) reaching the identical code path for scripted
  drives. Save dir `~/tools/cc-recomp/saves/` (env
  `CC_SAVESTATE_DIR`), file `slot1.ccsave` (~110–119 MB, save
  105–1,400 ms under load, load 41–276 ms).
- **Gates**: save **pause-only (state 14)** — gate v2, tightened
  after the state-13 tearing result below (Phase 1's "refuse
  outside pause" recommendation, confirmed the hard way). Load:
  front-end (this title idles in state **11**, measured — the
  first gate cut used <=6 and refused title loads), 13 or 14;
  state 12 and 7–10 refused. All refusals log one `[SAVE]`/`[LOAD]`
  line and touch nothing; the no-file and truncated-file load
  refusals were exercised at the title with the process unharmed.
- Build: `~/tools/cc-recomp/build-savestate` + launcher
  `run-savestate.sh` (the default `run.sh` is untouched).

### Results (drives 1–8, 2026-11-08; observables = per-slot
Thorium/life/stockpiles, ship record via bank+registry, per-bank
entity census + full byte-sum over both 256 KB banks)

- **Restore fidelity is byte-exact in every leg.** Pause save in
  Kreios L1 (29,118 pages, span 0x21011000, mission 1, mode 0):
  observables at the pause — Thorium 1000.0, life 25, stockpiles
  60x8, ship idx 89 hp 1000.0, bank byte-sums
  426499962924614703 / 7443478715371084856 — are reproduced
  *identically* after a same-session load over a diverged world
  and after a **cross-boot** load into a fresh process.
- **In-place / later-timeline loads wedge the sim** (Phase 1
  leg 3, confirmed in-glue): after a same-session load the state
  word is right and frames present, but two observable sets 30 s
  apart are bit-identical; inputs do not move the restored
  pause. Host state had advanced past the snapshot.
- **Title loads wedge the sim** (Phase 1 leg 5, confirmed
  in-glue): cross-boot load at state 11 restores state 14 and
  the exact world, which then never simulates.
- **State-13 saves tear**: a gameplay save loaded cross-boot
  killed the fresh process via the guest's own heap assertion
  (`mspace_malloc ... chunksize(p) == small_index2size(idx)`,
  then `sys_process_exit(1)`). Pause captures never showed
  this. Hence gate v2.
- **The leg-4 flow through the glue collapses the target match
  (drives 8–11)**. Four runs of the exact leg-4 recipe — fresh
  process, Kreios L1 entered by its own flow, paused, load —
  all ended the same deterministic way: the restore completes
  ([LOAD] 29,101–29,115 pages, 66–202 ms), the state word reads
  14, but the entity banks are already zeroed in the first
  post-load sample and the log shows an orderly match teardown
  (guest thread exits, boot level 152 reloading, state settling
  at 0, mission word reset). No crash, no resume. Three restore
  variants were tried against the identical flow: blanket
  memset of the span (drives 8/9), Phase-1-style reconciliation
  — zero only pages nonzero in the target but absent from the
  save (drive 10; also cut load time to 85 ms), and the Phase-1
  span cap of 512 MB on both capture and restore (drive 11,
  span 0x19200000). **All three refuted as causes** — the
  collapse is identical every time. The one structural
  difference that remains standing between Phase 1's working
  leg 4 and this one is **quiescence**: Phase 1 captured and
  restored with the whole process SIGSTOPped, while the in-glue
  restore copies ~115 MB over 66–200 ms with every thread live
  — the RSX thread draining a FIFO out of the memory being
  swapped, the audio task writing its rings, flip labels
  advancing — and the capture likewise scans a live (if
  pause-frozen) process whose host threads keep writing. The
  restored guest state therefore contains host-timeline
  artifacts (label/cursor values from the saving process's
  runtime) that the target's match-integrity logic rejects
  immediately. Making the in-glue restore sound needs a
  stop-the-world step — e.g. a boot-forked helper that
  SIGSTOPs the game and writes /proc/<pid>/mem exactly like
  the Phase-1 tool, driven by the glue — which is a v2 design
  step, not a patch.
- **Physical keys**: with a real window (Xvfb :99) and synthetic
  X11 keys, F5 in a pause wrote a new save ([SAVE] count 0->1,
  29,119 pages) and F9 restored it (count 3->4), observables
  exact. (The windowed front buffer itself never updated under
  llvmpipe — 29 present lines all session — so the visual
  before/after screenshot pair remains unverified; all state
  evidence is from guest RAM.)
- **Audio**: same-session load — music continues (RMS ~0.079
  sustained 45 s post-load, drive 6). Cross-boot title load —
  mix is exactly 0.0000 from ~2 s after the load for the rest of
  the session (drive 4), the leg-5 wedge taking the audio task
  with it; Phase 1's leg-4 resume kept healthy audio (peaks
  0.24–0.41). Phase 1's caution stands for any future in-match
  restore work: reset the host MP3 codec's stream table on
  restore (streams keyed by workspace pointer; a same-format
  restart is never detected, notes 040). v1 does not reset it.
- **Progression**: mission id / mode are *in* the snapshot
  (restored byte-exact: mission 1, mode 0), so a win in a
  resumed leg-4 match is scored by the game's own progression
  code for that mission and persisted by its own savedata
  write. The direct clear-and-credit check is still open: it
  needs a played-out win in a restored match, which today only
  the Phase-1 external leg-4 flow can reach (the in-glue
  restore blocks resumption — drives 8–11 above). Jerry's
  manual version, once a resume path is used: enter the level,
  restore, win it, and confirm the campaign map credits
  mission N and `NPUB30061-AUTO/SETTINGS.DAT` is rewritten.

### Updated verdict

Phase 2 ships the complete in-glue plumbing — hotkeys, format,
validation, gates, cache flush, byte-exact restore — and a
measured map of its limits. As of this writing the only
demonstrated end-to-end resume is Phase 1's external leg-4
flow; every in-glue load restores memory faithfully and then
fails to resume (frozen for in-place/title loads, an orderly
match teardown for cross-instance loads into a paused match).
The standing explanation is the missing stop-the-world step,
with the pad/SPURS/codec resync list from Phase 1 still owed
behind it. The README on this branch describes the feature
with exactly that status.

### Phase 2, round 2 — the Phase-1 resync list, applied and measured

Phase 1's follow-up directives were implemented in full and
re-tested (drives 12–13):

- **Pad resync (fork 2ffc2da4, build fix 98f63c11)**: the
  PAD_FILE/PAD_FILE2 injected-hold statics moved to file scope;
  `ps3_pad_resync_savestate()` clears both holds and marks every
  port fresh on load. It fires on every restore (log line
  confirmed).
- **Codec reset (fork fda534c8)**: `ps3_spu_mp3_reset_streams()`
  zeroes the stream table on load; slots re-initialise on next
  use. Fires on every restore (log line confirmed under
  PS3_CODEC_LOG).
- **Load gate v3 (glue)**: load is now **pause-only (state
  14)**, per the Phase-1 verdict — a state-13 load attempt
  refuses live (`[LOAD] refused: guest state 13 (need 14
  pause)`). Save was already pause-only (gate v2).
- **RSX backdrop**: the flush already invalidated presented
  surfaces as well as textures (engine surfaces + backend
  framebuffers both dropped — code-verified). A *visual*
  backdrop check is not achievable in the headless rig: the
  guest display buffers are all-zero in headless presents
  (three dumps from different scenes byte-identical), and the
  windowed front buffer never updated under llvmpipe. It is on
  Jerry's manual list below.
- **Outcomes**: the in-place restore is still byte-exact
  (drive 13: post-load observables == save observables,
  full equality) and still frozen. **Pad admissions still
  freeze** — with the resync in the path, START/CROSS/DOWN
  after an in-place load moved the `[cellPad] PAD_FILE press`
  count not at all (7 -> 7 -> 7 -> 7). Since the backend now
  provably resets its admission state, the freeze is *not* in
  the pad backend: after the restore the guest simply stops
  polling — cellPadGetData is never called again — which puts
  the wedge in guest thread synchronisation (the frame/input
  loop blocked on state the restore rolled back), the same
  wall as the sim freeze. The cross-instance (leg-4) load
  still collapses the target match (drive 12, identical
  shape: resync lines, 63 ms restore, banks zeroed in the
  first sample, state 0).
- **Level clear**: still unreachable — a restored match must
  resume before it can be won, and no in-glue load resumes
  yet. The different-mission pause load was not attempted:
  every load wedges before mission identity can matter.
- **Standing conclusion**: with pad, codec, RSX and gating
  all addressed, the one difference left between this and
  Phase 1's working leg-4 restore is the SIGSTOP
  stop-the-world (atomic capture and restore). That is the
  v2 design step; everything else on the resync list is now
  in and measured.


### Phase 2, round 3 — v2: stop-the-world restore, and the resync that wedged it (drives 14–17, 2026-11-08/09)

**Design (glue only; repo c9dec57).** `ss_do_load` keeps round
2's total validation and pause-only gates. Then, in order:
(1) the pad admission resync and the MP3 codec stream-table
reset fire while the world still runs (host bookkeeping only;
at the pause boundary guest match state is static, so they
cannot race the snapshot, and the stopped window stays down
to the page copy); (2) the `in_save` page bitmap is built;
(3) the parent opens `/proc/self/mem` `O_RDWR` and mmaps a
4 KiB `MAP_SHARED` status page; (4) `fork()` — the parent
`raise(SIGSTOP)`s immediately, freezing every game thread;
the child (raw syscalls only post-fork: no malloc, no stdio;
blob, bitmap, zero page and scratch are all pre-allocated)
waits for `/proc/<ppid>/stat` to report `T`, runs the
zero-fill reconciliation (pread each absent page, pwrite the
zero page only if nonzero), pwrites every stored page
through the inherited mem fd, publishes counts on the status
page, and `kill(ppid, SIGCONT)`s on **every** path before
`_exit`. The parent resumes inside the stop, waitpids, and
classifies: refusal-class failures (memfd/mmap/fork) happen
before any stop with guest memory untouched; a child that
wrote nothing leaves the session unharmed; a child that
failed mid-write is logged as guest memory possibly mixed —
consider the session corrupt and restart the game. The
`[LOAD]` line gains `stop-the-world <ms>, total <ms>`. Save
path and file format are unchanged; the Windows build keeps
the round-1 live copy (no stop-the-world equivalent).

**The bisect (the round's real finding).** Drive 14, first
v2 build — all three round 1/2 resyncs firing, RSX flush
included: the restore was byte-exact (post-load bank
byte-sums == save byte-sums) with a 226 ms stop, yet the
match did not resume — pad admissions frozen at 9, state
stuck at 14. (No collapse this time: round 2's leg-4 tore
down to state 0; atomicity fixed that much.) Drive 16 then
restored the *same save file* the Phase-1 way — external
SIGSTOP + `/proc/<pid>/mem` writes
(`recomp/savestate/load_ccsv.py`), no resyncs at all — into
a fresh process: **resumed** (admissions 7 → 8 on START,
state 13, bank byte-sums evolving over 55 s, byte-exact at
load). So the save content was never torn, and the copy's
atomicity was never the remaining problem: a resync was.
Removing all three resync calls from the glue (V2A) made
the in-glue load resume (drive 14 rerun: admissions 9 → 10,
state 13, presents 46 → 70 lines). The pad and codec
resyncs are provably host-local (two hold counters; an
8-entry stream table), so the RSX guest-cache flush is the
convicted one — it is the only resync whose effects reach
guest-visible machinery: the RSX FIFO and the surface
bookkeeping span the guest/host boundary, and the snapshot
already carries the guest half of that state; invalidating
the host half against a rolled-back guest half wedges the
guest's frame/input loop (it never calls cellPadGetData
again, while rendering continues). The flush's fork commits
(18ded31d, a9ec9227) stay in the fork — the exports are
simply no longer called by the glue. **Final shape (repo
5f6e1ed): pad + codec resyncs pre-stop; no RSX flush.**

**Results on the final build (drive 17 = the drive-14
battery, V2B).**

- **T1 (leg-4 in-glue) — RESUMES.** Session A save: 28,791
  pages, span 0x19200000, 184 ms. Session B (fresh boot, own
  Kreios 1 match, paused): `[LOAD] … 28791 pages (+5
  zero-filled) … stop-the-world 240 ms, total 240 ms`;
  post-load observables == save observables byte-exact (bank
  byte-sums 3249525225404672504 / 6193130047056848361);
  START admitted (admissions 9 → 10); state 14 → 13 and
  stayed; ~55 s later the bank byte-sums had evolved
  (7954374762107302580 / 7936554956303338685), presents
  43 → 74 lines, process alive. Session B's AUDIO_WAV grew
  to ~69.7 MB across the window — the mixer runs through
  the load (not sample-analysed this round).
- **T4 (refusals).** No save file → `[LOAD] refused: no
  save file …`, and the game stayed responsive in pause
  (admissions 7 → 8 on the next press). Truncated file →
  `[LOAD] refused: … corrupt or truncated; guest memory
  untouched` (admissions 8 → 9). Both are refused during
  total validation, before any fork or stop — a refused
  load cannot freeze the game.
- **T2 (in-place, later timeline; drive 15) — still
  wedges.** Save 27,799 pages (span 0x18F00000, 166 ms);
  after ~60 s of divergent play, load: 27,799 pages (+43
  zero-filled), stop 226 ms; post-load observables == save
  observables byte-exact — and admissions never moved again
  (9 → 9 on START and on DOWN), state stuck at 14, presents
  frozen. Same shape as Phase 1's leg 3 and round 2's
  drive 13: in this configuration the guest's own threads
  have run *past* the snapshot, and rolling memory back
  under them wedges their synchronisation in a way no
  host-side resync has fixed. Loading a save into a process
  that entered the same mission fresh (T1) is the
  supported flow; in-place rewind is not.
- **T3 (progression).** The mission id and mode ride in
  the snapshot header and restore byte-exact (mission 1,
  mode 0 in every pair above), so a clear in a resumed
  match is scored by the game's own progression code and
  persisted by its own savedata write — the in-memory
  prerequisites are verified. A *driven* clear was not
  achieved (driven unit sends have never worked in the
  headless rig), so the end-to-end win check stays on
  Jerry's manual list below — now against a build whose
  cross-process load resumes.
- **Stop durations**: 226–240 ms across the round's loads
  (~28.8k pages, ~113 MB written under SIGSTOP); total load
  wall time equals the stop time to the millisecond on the
  final build (everything before the stop is validation +
  bitmap).

**Still open.** In-place rewind (above); Battle-mode saves
(untested); the pause backdrop after F9 on a real display
(manual check 1 — headless presents carry no guest frame
content, unchanged); save compression and a build-identity
field in the header (future); whether the RSX-flush wedge
also explains round 1's in-place diorama backdrop (the
flush was in that path too — plausible, untested now that
in-place wedges earlier, at input).

**Jerry's manual checks (real display; a cross-process resume exists as of round 3 — run them on the savestate build)**:
(1) pause backdrop after F9 shows the *saved* scene, not the
scene you loaded from; (2) win the restored level and confirm
the campaign map credits the mission and
`NPUB30061-AUTO/SETTINGS.DAT` is rewritten; (3) music/SFX
behave across the load.

## Round 4 (2026-11-09): the real-user renderer failure — host FIFO walker position is not part of the restore

**The report.** Jerry hand-tested the savestate build on the
real GPU (mission 39, Campaign mode 0, Expansion content),
three attempts:

1. *In-place F9* (same session; the known-unsupported flow):
   the guest FIFO stream desynced — `[FIFODUMP] +0
   io=02189AB0 w=3CDD730D <-- bad word`, `[cellGcmSys] FIFO
   resync (unmapped-JUMP) 0x02189AB0 -> put 0x021B649C` —
   and the session froze moments later.
2. *The documented flow* (F5-pause-save in mission 39, quit,
   fresh launch, enter mission 39 by its own flow — Expansion
   asset loads, no errors — pause, F9): `[LOAD] … 19125 pages
   (+4 zero-filled), span 0x16600000, mission 39 mode 0
   restored over state 14, stop-the-world 245 ms` — then,
   immediately, `[RSX fp] sign-expand fetch opcode 25
   unsupported on unit 15` and `[RSX vulkan] engine: fragment
   translation failed: HLSL parse: ERROR: 0:18: '[' : array
   index out of range '51'`. Presents kept advancing
   (3360→4680+), total_surfs jumped 30→39 at the load,
   guest_draws ~193/frame, pad input live. The sim resumed;
   the screen was BLACK.
3. *Same flow again* (`[LOAD]` 18,526 pages, 244 ms): no FP
   error this time — instead the host FIFO parser desynced:
   `FIFO JUMP to unmapped IO 0x1FFBA310`, `[FIFOSTEP]` words
   at the ring base (io 0x00010000…) that are float payload
   (3F800000, 3EFE147A) parsed as commands, then `FIFO resync
   (unmapped-JUMP) 0x00010008 -> put 0x022D649C`, "resync
   rescued 137 fence(s)" across ~0x22D5494 bytes.

Both post-load signatures are the same divergence wearing
two masks (below). Attempt 1's signature is recorded as
**in-place blocker #2** — host FIFO parser position vs the
restored guest FIFO state — after thread sync (#1): an
in-place restore rewinds the guest ring but the host walker
keeps its current-session position, so it parses the rewound
stream from the wrong offset exactly as in attempt 3.

**Prior art.** The FP-translation failure is load-specific.
Grepping every driven log on the box: it fires immediately
after `[LOAD]` in the round-3 battery (ss_t14b: '57','51',
'55','57','51' back to back), after the round-1 live-copy
loads (ss_t8b, ss_t10), and after Phase-1-style external
restores (i2 ×3, i3 ×2; t16 shows the sign-expand warning
next to a garbage `[NV3089] SKIPPED fmt=0x6C 12288x273`
blit). No-load sessions never show it (the profiling agent's
campaign/battle logs and every drive's session A: zero).
Round 3 scored loads on admissions/census only, so the
failures sat unread in logs whose tests "passed" — the
pause backdrop those drives could not see was renderer-dead
in the failing runs. Whether a given load fails is
geometry-dependent (below), which is why other round-3
loads were clean.

**Headless repro.** Kreios leg-4 configuration (fresh boot,
own-flow match, pause, load a same-mission save): pre-load
and session-A counts zero; post-load **fp_fail=5** ('55' ×3
visible in the tail) with the restore byte-exact (post-load
bank sums == save-time sums) and the resume intact
(admissions advanced, state back to 13). A control run with
a benign walker geometry showed zero — the failure tracks
the stale-region geometry, not the save. Jerry's mission-39
save (preserved as `~/cc-savestates/slot1-mission39-repro
.ccsave`) loaded into a Kreios session exercises the same
path (the sim wedges cross-mission as before, but the
renderer signatures are the observable).

**Root cause.** The host FIFO walker (`cellGcmSys.c`,
`gcm_rsx_process_fifo_unlocked`) tracks its read position in
the host-private static `s_fifo_getoff` and chases the
guest-published put from there. The guest half of that
contract — the FIFO ring, the title's GCM context — is
guest RAM and *is* restored; the host half is not, and
nothing re-derived it. On resume the walker drains the
restored ring starting from the pre-load session's offset:

- If the stale offset lands on a saved-stream command
  boundary, it replays already-consumed history — mostly
  harmless state writes, which is why some loads were fine.
- If it lands mid-payload, inline vertex/uniform data is
  parsed as method headers: garbage methods poison the RSX
  state the draw engine consumes (an absurd 12288×273 blit
  in t16). The fragment-program address comes from host
  register state the walker feeds, so the decompiler ends
  up translating bytes that are not the guest's program;
  the walk produces constant references past the end of the
  `fp_constants[48]` cbuffer — `array index out of range
  '51'/'55'/'57'`, varying with the garbage — HLSL parse
  fails, the pipeline is dropped, every draw that needs it
  is dropped with it: black screen, live sim.
- If it runs into a word that decodes as a JUMP to unmapped
  IO, the walker's own resync skips to the guest's current
  put (attempts 1 and 3) — self-limiting for the parser,
  but everything between was applied as state.

Checked and exonerated: the FP memo (keyed on a
content fingerprint of the ucode itself — a poisoned fetch
is a different fingerprint, and the failing decompile is of
genuinely wrong bytes, not a stale memo hit) and the
persistent SPIR-V disk cache (a hit *skips* translation, so
it cannot produce a parse error; the repro fired with a
fresh cache directory).

**Fix.** Walker hold + resync, no cache flush (the round-2/3
lesson stands — guest-visible caches stay exactly as
restored):

- Fork, `cellGcmSys.c` (SAVESTATE FIFO RESYNC;
  `patch_fork_fifo_resync.py` + `_scan/_put/_scan2/_scan3`
  follow-ons): `ps3_gcm_fifo_savestate_hold()` sets a flag
  the drain entry checks first, parking the walker;
  `ps3_gcm_fifo_savestate_resync()` — under the FIFO lock —
  moves `s_fifo_getoff` to the restored guest's position:
  the IO offset of the restored title context's `current`
  (read through `gCellGcmCurrentContext`, the derivation
  the ring-recycle path uses; control-put fallback),
  scanned back to the saved session's last flip marker (see
  the flip half below). It clears the CALL return slot and
  the park/semaphore pass state, republishes the drained EA
  and the control get mirror at the walker position — and
  the control *put* from the restored guest position, never
  from the scanned-back walker start (an early version of
  the scan published the marker position as put; the walker
  chases put, believed itself caught up at the marker, and
  never consumed the tail). It logs one line per load:
  `[cellGcmSys] savestate load: FIFO walker 0x%08X -> 0x%08X
  (restored guest position)`.
- Glue (`patch_main_savestate.py` module; incremental
  applier `patch_main_savestate_fifo.py`): the hold fires
  with the pad/codec resyncs after total validation (closing
  the window between SIGCONT and the resync where the
  present thread could drain); the resync fires immediately
  after the restore on every outcome.

**The flip-completion half (found by the fix's own
validation).** The plain teleport stopped the renderer
errors — and stopped the resume: walker parked exactly at
the restored position, restore byte-exact, but the guest
never polled the pad again and presents froze. A gdb attach
to the stalled session showed the main PPU thread
usleep-polling a guest-RAM completion word and the libgcm
event thread idle in `sys_event_queue_receive`: the guest
was waiting on the completion of the last flip it had
queued in the saved session. Flips complete when the
walker *reaches* their FIFO marker (`s_fifo_flips_reached`
vs `s_fifo_flips_queued`); the saved session's last marker
sits behind the restored current, a walker teleported to
current never reaches it, and this session's counters never
counted it. The fork states the invariant itself, twice:
the resync skip-scan counts skipped markers ("a flip
marker skipped here will never be reached: count it, or
every flip after it waits for the drain forever") and the
no-IO-mapping write-off sets reached = queued ("do not let
a flip wait on them"). Pre-fix loads resumed precisely
because the stale walker *replayed* stream history and
crossed that marker on its way — resume was bootstrapped
by the same replay that poisoned the renderer. So the
resync (a) starts the walker AT the marker — executing it
is what notes the display buffer, raises the flip-pending
flag the present loop waits on, and counts the reach — and
(b) registers the saved flip in this session's books as
queued AND reached (one flip completion owed), so the tick
delivers exactly one completion and the counters stay
consistent for every later flip; if the saved flip had in
fact completed before the save, the pump's sequence stamp
absorbs the extra completion when no flip is outstanding.
Starting the walker a full frame earlier (at the previous
marker) was also tried and was *worse* (2 poisoned
pipelines vs 1): the register state a frame's draws inherit
predates the frame, so replaying it executes more draws
under mixed provenance, not fewer. This also re-reads
round 3's flush lesson: the RSX flush plausibly wedged
resume by disturbing this same completion bookkeeping, not
(only) by dropping surfaces.

**Validation (headless, scratch copy of the branch build,
deterministic Kreios leg-4 drives).** Before the fix, the
same drive: post-load fp_fail=5, resume only via the stale
replay. After: **resume passes with the renderer error
class gone** — restore byte-exact (post-load bank sums ==
save-time sums), admissions advance on START, state back
to 13 and evolving 55 s later, presents 37→55 — with
fp_fail=1: a single boundary draw (LOCAL offset 0x65A080,
a 16-word "program" whose bytes are float payload, index
'49'), in the first frame after the load. Jerry's
mission-39 save loaded into a Kreios session (cross-mission;
the sim cannot truly resume there, but the renderer is the
observable): walker resync line fires, presents advance,
fp_fail=3 — the same boundary class at cross-mission scale.
Noload control: zero signatures throughout.

The residual is understood and bounded: a draw whose
shader-program address was set by stream history before the
saved session's tail, and whose local-memory contents
differ between the two sessions, fetches the other
session's bytes when its pipeline is first created after
the load. It is confined to the first frame(s) after a
load — the guest's own state stream re-covers every
material as it plays — and it is *not* the reported bug:
Jerry's screen stayed black because the stale walker
poisoned state continuously, not for one frame. Closing
the residual for good means carrying the RSX register/local
state in the snapshot (a format change; v-next candidate),
not another host-side heuristic. What headless still
cannot prove is the picture: the real-GPU check is Jerry's
— reload the mission-39 save by the documented flow and
confirm the scene renders (and the pause backdrop, manual
check 1 from round 3). Expect at most a one-frame glitch at
the load instant, not a black screen.
