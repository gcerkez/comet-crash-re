# Runtime Patches to ps3recomp

This file documents all changes applied to the toolkit clone (`~/tools/ps3recomp`).

---

### 1. `libs/codec/cellPngDec.c`: Validate and load source data during `cellPngDecOpen`

- **File**: `libs/codec/cellPngDec.c` (in `cellPngDecOpen`)
- **What Changed**: Added a call to `pngdec_load_source(sub)` inside `cellPngDecOpen`. If loading fails (e.g. file not found or invalid memory buffer), `cellPngDecOpen` resets the subhandle (`sub->in_use = 0`) and returns `CELL_PNGDEC_ERROR_OPEN_FILE` (`0x80611151`).
- **Why**: According to the Sony PS3 Cell SDK specification, `cellPngDecOpen` must attempt to open the file stream and return `CELL_PNGDEC_ERROR_OPEN_FILE` if the file cannot be opened. The previous implementation deferred file opening to `cellPngDecReadHeader` and always returned `CELL_OK` from `cellPngDecOpen`. Comet Crash's PNG loader (`FUN_0016eb54` @ `0x0016eb54`) checks the return value of `cellPngDecOpen`; when it blindly received `CELL_OK`, it proceeded to call `cellPngDecReadHeader`, did not check the header return code, and proceeded to execute an indirect branch on uninitialized stack memory. Returning `CELL_PNGDEC_ERROR_OPEN_FILE` from `cellPngDecOpen` allows `FUN_0016eb54` to exit cleanly on missing files.
- **Upstreamable**: **YES, definitely upstreamable**. This conforms directly to official Sony Cell SDK behavior and RPCS3's `cellPngDecOpen` error semantics.

---

### 2. `runtime/syscalls/sys_fs.c` & `runtime/ppu/ppu_fs.cpp`: Unified file descriptor allocation

- **Files**: `runtime/syscalls/sys_fs.c`, `runtime/ppu/ppu_fs.cpp`
- **What Changed**: Exported `sys_fs_claim_fd(void* host_file_or_stream)` and `sys_fs_free_fd(int fd)` from `sys_fs.c` and routed `cellFsSdataOpen` in `ppu_fs.cpp` through the shared file descriptor table instead of maintaining a disjoint table.
- **Why**: `cellFsSdataOpen` allocated file descriptors in an isolated table in `ppu_fs.cpp`, but subsequent `cellFsRead` / `sys_fs_read` syscalls looked up the descriptor in `sys_fs.c`'s `s_fds` table. As a result, reading `package.lua.sdat` failed with `EBADF` (9), preventing game startup scripts from loading.
- **Upstreamable**: **YES, definitely upstreamable**. All LV2 / CellFs file descriptor operations must share a single descriptor namespace.

---

### 3. `libs/network/sceNp.c`: Check file existence in `np_drm_is_available` (`sceNpDrmIsAvailable` / `sceNpDrmIsAvailable2`)

- **File**: `libs/network/sceNp.c` (in `np_drm_is_available`)
- **What Changed**: Translated `drm_path` via `cellfs_translate_path` and verified file existence with `fopen`. If the DRM file (`.edat`) is missing or cannot be opened, returns `0x80029513` (`SCE_NP_DRM_ERROR_FILE_NOT_FOUND`) instead of blindly returning `CELL_OK` (`0`).
- **Why**: Comet Crash checks whether DLC 1 is installed via `FUN_0016ea30` (`0x0016ea30`), which queries `sceNpDrmIsAvailable2(..., ".../data/dlc1.edat")`. When `sceNpDrmIsAvailable2` returned `CELL_OK` on missing `dlc1.edat`, the game assumed DLC 1 was installed (`dlcStatus = 2`), and attempted to load DLC asset packs (`data3.pak`, `data4.pak`, `data5.pak`). Because those files do not exist in the base package, `FUN_001648d0` failed with `file load error: .../data3.pak`, set global error flag `*0x00873065 = 1` (`g_hasError = 1`), and triggered an immediate abort to `cellGameContentErrorDialog(101)` (`BROKEN_EXIT_HDDGAME`).
- **Upstreamable**: **YES, definitely upstreamable**. On real PS3, checking availability of non-existent DRM content returns `0x80029513`.

---

### 4. `libs/spurs/cellSpurs.c`, `libs/spurs/cellSpurs.h`, `tools/nid_database.py`: Implementation of `cellSpursAddUrgentCommand`, `cellSpursAddUrgentCall`, and `JOBLIST` (Opcode 6)

- **Files**: `libs/spurs/cellSpurs.c`, `libs/spurs/cellSpurs.h`, `tools/nid_database.py`
- **What Changed**:
  1. Added `cellSpursAddUrgentCommand(u64 jc_ea, u64 cmd)` (NID `0x17001000`) and `cellSpursAddUrgentCall(u64 jc_ea, u64 cmd_list_ea)` (NID `0x8FDF50B2`). Writes commands into guest `urgentCmds[4]` at `jc_ea + 0x30` and queues into `s_jobchains[slot].urgent_cmd`.
  2. Updated `jc_execute` to poll and process urgent commands prior to fetching from `pc`: handles `RESET_PC` (opcode 1, sets `pc`), `CALL` (opcode 4, saves `ret_pc`), and `JOB` (opcode 0, executes job synchronously).
  3. Implemented `JOBLIST` command (opcode 6): reads `CellSpursJobList` header at `jl_ea` (`numJobs`, `sizeOfJob`, `eaJobList`) and dispatches all constituent jobs.
  4. On `END` command (opcode 7, extension 127 = 0x7F): checks if the chain has an associated `CellSpursEventFlag` at `jc_ea + 0x580` (`ef_sync_find(jc_ea + 0x580)`), and sets bit 1 to unblock waiters.
- **Why**: Comet Crash's main thread dispatches SPU background workloads (via `FUN_0016b91c` @ `0x0016b91c`) by appending a `NEXT` pointer to its command buffer, issuing `cellSpursAddUrgentCommand(0x002c5780, cmd | 1)` to reset PC to the command buffer, and running `cellSpursRunJobChain`. Because `cellSpursAddUrgentCommand` was an unresolved NID stub, urgent commands were dropped, the job chain immediately terminated on its empty initial entry (`0x7F` END), the completion SPU job at `0x00186980` never ran, and the main thread blocked forever in `cellSpursEventFlagWait(0x002c5d00, &bit1, 1)`.
- **Upstreamable**: **YES, definitely upstreamable**. This completes core SPURS job chain execution semantics required by titles using urgent command injection.

---

### 5. `libs/video/rsx_vulkan_backend.c`: Tolerate `VK_INCOMPLETE` in `vkGetPhysicalDeviceSurfaceFormatsKHR`

- **File**: `libs/video/rsx_vulkan_backend.c` (in `vk_create_swapchain`)
- **What Changed**: Allowed `VK_INCOMPLETE` (`5`) alongside `VK_SUCCESS` when fetching surface formats into the fixed 64-element `fmts` array (`sfr != VK_SUCCESS && sfr != VK_INCOMPLETE`).
- **Why**: On modern GPUs and drivers (such as AMD Radeon RX 7900 XTX with RADV under Wayland), the surface supports more than 64 formats. `rsx_vulkan_backend.c` clamped `nf` to 64: `if (nf > 64) nf = 64;`. According to the Vulkan specification, when `pSurfaceFormatCount` is less than the total supported formats, `vkGetPhysicalDeviceSurfaceFormatsKHR` returns `VK_INCOMPLETE` (`5`). The macro `VK_CHECK` treated `VK_INCOMPLETE` as an error, aborted swapchain creation with `[RSX vulkan] vkGetPhysicalDeviceSurfaceFormatsKHR failed (VkResult 5)`, and invoked `vk_window_drop()`, disabling the window and falling back to headless mode. Tolerating `VK_INCOMPLETE` allows swapchain creation to inspect the first 64 formats and create the real window swapchain.
### 6. `libs/video/cellGcmSys.c`: Correct FIFO `CALL` command encoding check

- **File**: `libs/video/cellGcmSys.c` (in `gcm_rsx_process_fifo_unlocked`)
- **What Changed**: Changed `if ((w & 3) == 2)` to `if ((w & 0xE0000003u) == 2u)`. Also updated the control flow trace and statistics checks (`0xE0000003u`).
- **Why**: Under NVIDIA NV40 pusher command encoding, subroutine `CALL` words are encoded with bits 31-29 = `000` and bits 1-0 = `10` (`(w & 0xE0000003u) == 2u`). The loose check `(w & 3) == 2` erroneously matched any non-increasing method word (`type == 2`, bits 31-29 = `010`) whose method offset had bit 1 set, such as `0x567D1F52`. When Comet Crash submitted vertex constants using non-increasing method `0x567D1F52`, `cellGcmSys.c` misidentified the method as a `CALL 0x167D1F50`, treated `0x167D1F50` as an unmapped IO address, terminated FIFO draining with `[cellGcmSys] FIFO CALL to unmapped IO 0x167D1F50 (word 0x567D1F52) -- not taken, drain stops here`, and reset the FIFO get offset to 0, completely halting scene rendering.
- **Upstreamable**: **YES, definitely upstreamable**. This conforms to the NV40 / RSX command encoding specification.



---
### 7. `libs/video/rsx_draw_engine.c`: GPU NV3089 blit declines antialiased surfaces

- **File**: `libs/video/rsx_draw_engine.c` (in `rsx_draw_engine_blit`)
- **What Changed**: The GPU fast path now returns 0 (fall back to the guest-memory copy in `cellGcmSys`) when the source or destination engine surface is antialiased — `(aa_grid & 0xFF) > 1 || (aa_grid >> 8) > 1`. Non-AA blits are unchanged. Committed in the fork as b8e5cbb7 (2026-11-07, at Jerry's direction).
- **Why**: Engine surfaces for AA render targets are held at SAMPLE resolution (upstream f752e40f: a 1280x720 target with a 2x2 grid is a 2560x1440 surface), but an NV3089 blit's geometry arrives in the guest's LOGICAL pixels. The fast path (fork commit c8562f37) passed the logical rectangle straight to `vkCmdBlitImage`, so a blit of Comet Crash's scene target copied the top-left 1280x720 samples of the 2560x1440 grid — one quadrant of the composed frame — and the game's post chain presented that crop as the full picture: the arena board rendered as a ~2x zoom into the enemy-base corner, and HUD glyph surfaces moved by the same path lost their text. The pre-c8562f37 guest-memory copy handled these blits correctly. Verified by A/B: `RSX_GPU_BLIT=0` (stock everything else) restored the full-board overview and the complete HUD (timer, Thorium 100, unit counts); with the decline compiled in and GPU blits re-enabled, the same run reaches the identical correct frame. Root-cause write-up: comet-crash-re `notes/RECONSTRUCTION-035.md`.
- **Upstreamable**: **Yes, as a correctness guard** — a GPU blit between mismatched coordinate spaces (logical guest pixels vs sample-resolution surfaces) should either translate the rectangle by the aa grid or decline; declining is the conservative choice. A future refinement could scale the source rectangle by the grid and let the blit double as the AA resolve when the destination is non-AA.

### 8. `libs/video/rsx_vulkan_backend.c`: Quit confirmation hook in the window pump

- **File**: `libs/video/rsx_vulkan_backend.c` (pump), plus the per-title hook in cc-recomp `main.cpp`
- **What Changed**: `rsx_vulkan_backend_pump_messages` quit instantly on SDL_QUIT (window close) or Escape — a stray Escape threw away a run in progress (Jerry's report). The pump now consults the weak title hook `ps3_host_quit_needs_confirm()`; when it returns nonzero, an SDL message box asks ("Keep playing" / "Quit", Escape defaults to Keep playing, and if the dialog cannot be shown the quit proceeds rather than trapping the user). Comet Crash's hook reads the guest state word (DAT_005520C0) and asks only in the arena states 13 (gameplay) and 14 (pause / battle end); menus still quit immediately. Committed in the fork as a8bef568.
- **Why**: Losing a 20-minute run to one accidental keypress is the kind of papercut that makes a port feel broken. The hook is title-side because only the title knows what a quit costs; the backend stays generic.
- **Verified**: headless on Xvfb — Escape in state 13 opened the dialog and froze presents; "Keep playing" resumed the run (log growth + flips advancing); a second dialog's "Quit" exited the process.
- **Upstreamable**: **Yes** — the weak-hook pattern matches how this tree already does per-title policy (cf. the semaphore job-completion hook in `sys_semaphore.c`).

### 9. Log quieting: `libs/system/sysPrxForUser.c` gate + launcher defaults

- **File**: `libs/system/sysPrxForUser.c`; launchers `~/tools/cc-recomp/run.sh` and comet-crash-re `recomp/run.sh`
- **What Changed**: (a) `sys_lwcond_wait`'s `[WAIT]` fprintf was the one per-wait printer not gated behind `ps3_log_verbose()` (runtime/ps3_log.h); it is gated now, like its semaphore/event siblings. Committed in the fork as 71a0bc14. (b) Both launchers defaulted `PS3_VERBOSE=1` (tools copy, hardcoded) and `PAD_DEBUG_LOG` / `CC_CAM_DIAG` / `PS3RECOMP_VP_DIAG` to 1; all four now default to 0 and stay one env var away for diagnosis.
- **Why**: Jerry's runs spewed several lines per frame — the two `[RSX_VP]` lines, the `[CC_CAM]` line, pad logs, and, because PS3_VERBOSE=1 forces every gated printer on, the `[spurs-job]` dispatch/return pair (~2 per job at ~1500 jobs/s, each followed by an fflush through the single stderr FILE lock). ps3_log.h's own header documents this exact failure mode starving guest threads into user-visible hitches in other titles — it is also the prime suspect for the intermittent ship "blip" Jerry reported, since the SPURS dispatch thread feeds the render pipeline.
- **Verified**: 60 s boot-to-menu via run.sh dropped from ~13 KB/s of log to ~1.9 KB/s, and the steady state is a heartbeat: `[RSX_PRESENT]` every 60 frames plus a `[spu_workload]` milestone every 500 signals.
- **Upstreamable**: **Yes** for the lwcond gate (consistency fix); the launcher defaults are per-title.


### 10. `libs/input/cellPad.c`: `PAD_FILE2` — closed-loop pad driving for port 1

- **File**: `libs/input/cellPad.c`
- **What Changed**: Two halves. (a) PAD_FILE2=<path> mirrors the PAD_FILE contract for port 1 (same "0xMASK [count]" encoding, consumed on admission, held per guest report). (b) While PAD_FILE2 is set, `pad_poll_backend` marks `s_host_state[1].connected = 1` before `pad_track_assign()`, so the port reads as present from cellPadInit's baseline. Committed in the fork as 7adc9739.
- **Why**: Every scripted mechanism (PAD_FILE, PAD_SCRIPT, PAD_AUTOPRESS) is port-0 only, and YDKJ_INJECT_PAD's 15/180-poll pulse races the guest's per-frame sampling — a second player could not be driven headless at all, which blocked reaching Battle mode (its join screen needs START + Ready on pad 2) while reproducing a rendering bug. The connected half is load-bearing: titles discover pads through cellPadGetInfo and never poll a port they believe is empty, so without it GetData(1) is never called and the file is never read.
- **Verified**: a 2P Battle match joined, readied and started entirely from the two files (Kreios area 1, terrain level101.map). Investigation record: comet-crash-re `notes/RECONSTRUCTION-036.md`.
- **Upstreamable**: **Yes** — it completes the existing PAD_FILE mechanism rather than adding a parallel one.


## 11. 1080p present size follows PS3_VIDEO_RES (cc-recomp glue)

The Vulkan backend creates its offscreen present target and its window
ONCE at the size passed to rsx_backend_init and never resizes them;
vk_eng_present blits the guest's display surface into that target every
flip. The glue hardcoded 1280x720, so when the guest selected 1920x1080
(PS3_VIDEO_RES unset or "1080" makes cellVideoOut offer it, and this
title takes it), every 1080p frame was downscaled into the 720p target
-- and maximising the window just stretched the 720p picture back up,
which read as "no real 1080p, blocky when scaled". (The game itself
needs no Resc work: it never calls cellResc; it renders directly into
1920x1080 display buffers.)

Fix (main.cpp, comet_window_size): the backend init size is derived
from the same PS3_VIDEO_RES variable the cellVideoOut HLE reads --
"1080" inits at 1920x1080, anything else keeps 1280x720. Both
launchers (tools run.sh, repo recomp/run.sh) now default
PS3_VIDEO_RES to 1080. Verified headless at 1920x1080: the SDL window
opens at 1920x1080+0+0 and gameplay fills the full frame (complete
HUD, no letterbox). A backend-side dynamic resize on presented-surface
size change remains the general-title follow-up; the init-size route
covers mode-at-boot titles like this one.


## 12. Right-stick range boost in cellPad (notes 038)

Titles tuned on a DualShock 3 put hard-tilt gates near full scale --
Comet Crash's unit-send commit needs converted stick magnitude over
0.9 (raw bytes ~246+), which the DS3 reaches by saturating at the
rim. The SDL/XInput fills mapped a modern pad's circular gate
linearly, so a full tilt landed at ~218-245: the send menu engaged
but the selection lock never set and auto-send never started.

pad_boost_right_axis() in libs/input/cellPad.c now stretches the
right stick's deflection about the centre (default x1.25, clamped to
the byte range) in both backend fills. Centre and converter
deadzones are untouched; the left stick is left alone. The gain is
tunable via PAD_STICK_BOOST (1.0 = off, clamped 1.0-2.0). Verified:
rebuild + headless boot smoke (presents normally); the functional
retest needs the real controller in the player's hands.


## 13. Native host MP3 codec at the MultiStream PIC seam (notes 039/040)

The game's audio is all MP3, decoded by MultiStream calling a
codec plugin it loads from /dev_flash/sys/external/flashMP3.pic.
This project uses no Sony firmware, so the plugin is implemented
on the host: when spu_indirect_branch reaches the codec entry
range [0x1A900, 0x21000), ps3_spu_mp3_pic_invoke()
(runtime/spu/spu_mp3_codec.c) answers with the protocol the
lifted caller expects -- param+0x00 = bytes consumed (every
return), param+0x10 = PCM bytes, r3 = PCM address in the
codec-owned LS range at 0x1C900 or 0 for "no complete frame".
Decoding is minimp3 (public domain, vendored at
runtime/spu/minimp3.h). Two details cost a test cycle each: site
A's priming call ignores the returned pointer, so a frame decoded
there is held for the site-B call; and the PCM must be written
big-endian (the real plugin is SPU code) -- little-endian samples
pinned the mixer at full scale. Verified by AUDIO_PEAK: menu
music decodes continuously (417-byte first frame, 104-byte VBR
frames after, as encoded) with living peak levels.
PS3_NO_HOST_CODEC=1 restores the stub; PS3_CODEC_LOG=1 traces.


## 14. Guest Cheats submenu in Options (lift patch, stage 2)

The lifted menu cluster (one host function, func_000F6990) is
patched by recomp/scripts/patch_lift_cheats.py, applied by setup.py
after every fresh lift (assert-anchored, idempotent). Four edits:

1. The Options builder gains a "Cheats" item after Credits
   (opcode 31), mirroring the Credits append but with a planted
   label -- the game has no "Cheats" string of its own.
2. op_dispatch intercepts five opcodes the guest table leaves on
   its no-op handler, ahead of the table read: 31 builds the Cheats
   submenu; 14/30/37/47 flip the Thorium / Buildables / Stockpiles /
   Health toggles in the glue (comet_cheats_flip).
3. The submenu builder mirrors the game's own Video Record submenu
   (guest FUN_000fa0b4) construction: same container ctor, title
   setter, append calls and layout/push tail, with one row per
   toggle. Row labels are chosen at build time from the live toggle
   state ("Thorium: On" / "Thorium: Off"), so a flip shows on the
   next open; flips do not rebuild the open menu.
4. Labels live in guest memory at 0x21010000, planted by the glue on
   its first tick -- above the ELF image and the runtime's inject
   window, below the SPU windows, in a range the game never
   addresses. All labels are <= 15 chars, so the game's string type
   stays in its small-string form (no heap, no destructor risk).
   Toggle state itself lives in the glue (env / control file /
   comet_cheats_get), never in guest memory.

Verified in driven sessions: the Options menu shows Cheats after
Credits; the submenu renders the four rows with correct On/Off
state; flips take effect immediately and read back correctly on
reopen; the stage-1 pins engage from menu flips exactly as from
env/file control.


## 15. Cheats entry in the pause menu's Settings (lift patch, Edit 6)

The in-game pause menu's Settings screen is a separate builder
(guest FUN_000f88c8, dispatch opcode 74), not the Options builder
with items gated off -- the Zoom/Language/Credits appends simply do
not exist in it. Edit 6 of patch_lift_cheats.py appends the same
opcode-31 "Cheats" item at that builder's convergent tail
(loc_000F8B7C), after its four stock items. Differences from the
Options append, both forced by that builder: the string temp is
gpr[1]+0x86C (free there; its fields are unreferenced in the
builder) and the temp pointer travels in gpr[20], because gpr[19]
carries the item-vector pointer in F88C8's tail. The submenu build,
the opcode intercepts and the planted labels are shared with
entry 14, so the Cheats screen reached from pause is the same one,
and a flip made from pause applies to the paused match at once
(the pins gate on game states 13/14).

Verified in a driven session: pause -> Settings shows Cheats as the
fifth item; the submenu opens over the paused game; a flip made
there (Health Off -> On) reads back correctly on reopen.

## 16. NV3089 copies out of AA surfaces take the GPU blit (fork 9ec0d82c)

`rsx_draw_engine_blit` declined every blit whose source was an
antialiased surface, so this title's per-frame scene-to-effect-texture
copies (1024x1024 -> 512x512 at ds=dt=2, ~3 per frame) fell back to a
full guest-memory readback (with a pipeline drain), a CPU nearest
downscale, and a texture re-decode on next sample. The fix drops the
source-side decline, clamps the source rectangle against the sample
extent instead of the logical dims, and keeps AA sources on nearest
sampling (the fallback's semantics). Destination-side AA still
declines; non-AA blits are unchanged. Measured on the desktop's RX
7900 XTX (1920x1080, same driven scene, 60 s window): 37.0 -> 71.0
guest fps, and the readback/decode complex no longer appears in
FIFO-thread stack samples. Full investigation: notes 042.

## 17. Blit destinations sized from pitch, not one blit's rect (fork 4a6101c4)

`rsx_draw_engine_blit` created a fresh destination surface at the
blit's own rect size and offset. This title writes its scene-resolve
texture as a grid of twelve offset-addressed NV3089 tiles per frame,
so each tile spawned its own fragment surface; the texture sampler
resolves the buffer's base offset, and the frame sampled a single
tile stretched over the screen -- the gameplay view looked locked
into the board's bottom-left corner at high magnification, split by
a seam. The fix sizes a newly created blit destination from the
destination pitch (the buffer's row width), and a frame-wide buffer
takes the frame's height as well, so the first tile creates the
whole surface and the rest resolve into it. Verified: all twelve
tiles land in one surface at their exact rects, the presented frame
shows the whole board, and real-GPU frame rate is unchanged (71 fps
over a 60 s window, the entry-16 protocol). Full investigation:
notes 043.

## 18. Backend blits clip an edge overshoot instead of dropping (fork 4a97c240)

`vk_eng_color_target_blit` silently returned whenever a blit rect
overshot either surface's extent. This title's twelve scene-resolve
tiles use (1,1)-based source origins in sample space, so the far
column ends at 3073+768 = 3841 > 3840 and the far row at
2049+112 = 2161 > 2160: six of the twelve tiles were dropped every
frame, and the resolve texture's right 384 columns and bottom 56
rows stayed black — a hard black band at the frame's right edge in
gameplay (the bottom loss hid under the HUD bar). The RSX clips such
reads at the surface edge, as does the CPU fallback path. The fix
shrinks only the overflowing source extent (the destination keeps
its size; the edge texel stretches by less than one sample), shrinks
both extents proportionally on a destination overshoot, and still
drops blits that start outside either surface. Verified: the
pass-dump display target is full width (content columns 9..1909,
was 9..1535) with the bottom strip restored, a real-GPU window shows
content to its right edge, and the entry-16 frame-rate protocol
reads 72 fps over 60 s. Full investigation: notes 044.

## 19. MP3 codec seam: validate frames, rebuild the reservoir, never wedge (fork, spu_mp3_codec.c)

The MultiStream caller is frame-synchronous: it stages one MP3 frame
and advances only by the codec's reported `consumed`. The original
host codec trusted minimp3's byte count and kept one persistent
decoder across streams, so a single failed or misparsed frame
(a straddle over stale staged bytes, or a stream switch polluting
the bit reservoir) broke the reservoir chain for every later frame
while `consumed=0` froze the caller's staging -- all audio died
~60 s into a session, at match entry, and never returned
(206k calls, 182k of them spinning on one frame). The seam now:
(1) accepts a decode only when minimp3's byte count equals the
header-computed frame length; (2) treats a format change as a new
stream (decoder + tail reset); (3) keeps a whole-frame tail of
recently consumed frames and, on failure, replays it through a
fresh decoder to rebuild the bit reservoir exactly before decoding
the staged frame; (4) as a last resort consumes the header length
and emits one silent frame, so the caller's staging always
advances. Verified on the drive that wedged before: 9,399 calls,
99.1% consuming, uniform through the match, real PCM throughout.
Full investigation: notes 045.

## 20. Cheats toggles refresh in place: unlink, then rebuild (lift patch, amends 14/15)

A flip originally returned without rebuilding, so the row label
showed the old state until the submenu was reopened. A bare
builder re-run fixed the label but stacked a stale submenu copy
on every flip: the builder inserts the fresh menu at the menu-list
anchor (game object + 0x53654C, guest 0x844B4C) without removing
the old one, and Circle then waded back through outdated copies.
Each flip block now first calls the game's own unlink-and-close
helper FUN_001339E0 with the anchor address -- removing exactly
the open submenu, as a Circle press does -- and then re-runs the
builder. Driven-verified from both Options and the pause menu's
Settings: the flipped row's label updates immediately, one Circle
exits to the parent menu with its selection preserved, and the
chain length stays constant. One cosmetic remainder: the rebuilt
submenu's highlight restarts on the first row. Investigation:
notes 041 (redraw addendum).

## 21. Pad reports at a vblank-like cadence (fork 1c4bd6b2)

pad_poll_backend served a fresh pad report every 4 ms (250 Hz).
Comet Crash counts fresh reports for its input timing (menu
auto-repeat), so in states whose loop polls faster than 60 Hz
the timing ran up to ~4x fast: on the real GPU the pause loop
polls ~280x/s while front-end states poll slower, which is why
pause menus overshot options and the front-end menus felt fine.
The poll gate now defaults to a 16 ms cadence (one fresh report
per vblank, as on hardware); PAD_POLL_INTERVAL_MS overrides
(4 restores the old behaviour). Full investigation: notes 046.

## 22. Optional anti-clip mix normalize (fork d7798d41)

The cellAudio mixer summed all ports into a block and hard-clipped
to [-1, 1], squaring stacked peaks (the leading suspect for sounds
"overwhelming" when several play at once; notes 047). With
AUDIO_MIX_NORM=1, a block that would clip is scaled uniformly to
peak 1.0 instead; sub-full-scale blocks are bit-identical, so solo
sounds never change. Default off pending a heavy-load verdict.

## 23. Fragment-program walk memoization (fork dc9878b2)

eng_pipeline_get re-walked the fragment program five times per
draw (texture units, gamma units, expand units, structural hash,
inline constants) before its pipeline cache lookup -- the FIFO
thread's top cost in sprite-heavy scenes (live profile: ~380%
CPU, GPU 5-38%). The five results are pure functions of the
program bytes (+ the gamma/expand input masks) and are now
memoized on an FNV content fingerprint: one byte pass replaces
five decode walks. Failure semantics preserved (malformed
program and collect failure still return 0; >256-constant
programs take the direct path). Driven-session counters: 72%
of 327,680 lookups served from the memo; rendering verified in
a live match; frame-rate A/B neutral under identical conditions.
Full investigation: notes 048.

## 24. Cheats stage 3: per-player Battle cheats (repo)

Cheat toggles are per player (notes 049). The Cheats submenu gains
a "Player: N" selector row (opcode 49; glue bridge
comet_cheats_get_player / comet_cheats_cycle_player); the four
toggle rows beneath it show and flip the selected player's state.
The glue keeps s_cheats[player][toggle] and the tick maps players
to stat-array slots by the mode word (obj+0x55209C: 2 = Battle,
Player N -> slot N-1; otherwise Player 1 -> slot 1, the Campaign
human, and slot 0 is never pinned). Thorium pins the per-slot
float channel (obj+0x348 + slot*0x80, the authoritative balance);
the u32 shadow pair keeps following the slot-1 player. Control
file gains p2_/p3_/p4_ prefixed lines; env gains
CC_CHEAT_P<N>_<NAME>. Buildables' shared requirement table still
neutralises globally when any player enables it (the per-seat
tech-mask bits stay individual). Verified driven: submenu
cycling/flips from Options; a 2P Battle (Player 2 Thorium pinned
at 1000 while Player 1 stayed 550; Player 1's health pin restored
a poked life while Player 2's stayed down); a Campaign regression
run (human slot 1 pinned as before, AI untouched).

## #25 — SPU geometry job 00187280: native override (on by default)

The SPURS geometry batch job (guest 0x187280: per-group matrix
compose, vertex transform + normal normalize, triangle frustum
culling; kicked and awaited every frame) is replaced at its inner
call site by a native transcription in the fork
(runtime/spu/spu_job187280.c, ps3_job187280_compute_native). The
job issues no GETs — inputs are staged into LS by the runtime and
its three chunked write-back loops are PUTs — so the override
computes in LS exactly where the lifted code does. Groups stay
sequential (shared scratch, loop-carried cursors); element passes
and the cull test phase parallelise inside a group. Equivalence:
0 failures across 3,033 harness cases (byte-identical 256 KB LS
plus live registers, three runs). Patch mechanics:
recomp/scripts/patch_job_override.py swaps the callee in the
generated job TU and adds the workspace glue file to the build;
setup.py applies it in the standard pipeline, so the override is
**on by default**. PS3_NO_JOB_OVERRIDE=1 (non-empty) restores the
lifted job — the A/B and debugging switch. Measured: 2.3–3.1x
faster per call on transform-heavy shapes; in a heavy Battle the
job vanishes from profile samples (the entity-sim job 00188300
becomes the wall; see notes 050). Details: notes 050.

**Pool tuning (fork 311a45a7).** A bench decomposition of the
cull-heavy shape (16 groups x 128 elements) showed its groups fell
below the element-pass pool floor (256 items), so every group's
transform pass ran serially on the calling thread (~1.5 ms of the
6.3 ms call). The element-pass threshold is now 128 with one chunk
per worker, and both pool waits spin briefly before parking (256
worker / 1000 caller pauses; long spins regress under CPU
contention - 15 spinning workers starve the caller's serial
phases). Cull-heavy: 6.32 -> 4.49 ms per call (-29%, medians of 6
on a contended 16-core host); transform shapes flat to -6%;
equivalence harness 0/3033 failures, three consecutive runs,
against the byte-identical TU.

## #26 — Cheats stage 4: the Ship toggle, plus the stockpile aggregate fix

Two changes, both in the host cheats module / lift patch family
(#14/#15/#20/#24 lineage):

**Ship (invulnerable player ship).** The ship is a type-1 entity in
the game's double-buffered 256-byte entity banks (bank bases are the
u32s at obj+0x180 / obj+0x198; the per-player ship record index is
the u16 at obj+0x36C8 + 2*slot; bank A = 0x10800200, bank B =
0x13500100 in observed sessions). Health is the float at record
+0x50, max the float at +0x64 (1000.0, stamped at creation by
FUN_0011c4f8 from the constant at ELF VA 0x1F5A90). All damage,
healing and death run inside SPU job 00188300: deltas apply as
health = min(max, health + delta) with no lower clamp; the death
path triggers on health <= 0 and writes health := 1.0f before the
record becomes a type-63 tombstone or respawns at 1000.0. Verified
against all 61 hunt-7 RAM dumps and confirmed live: an external
poke of 500.0 stuck and then rose at the base-repair rate
(~+122/s) in both banks. The glue tick, per enabled player, pins
record +0x50 := record +0x64 in both banks (type byte +0x0F == 1
checked first; registry index 0 falls back to a scan for the first
type-1 record so solo play always resolves). Menu: a fifth toggle
row "Ship: On/Off" under Health, opcode 57, control-file key
ship= / pN_ship=, env seed CC_CHEAT_SHIP / CC_CHEAT_P<N>_SHIP.
Which registry slot a second human's ship takes in Battle is
unobserved; the mapping follows the stat-slot order.

**Stockpile aggregate fix.** The cheat pinned the eight per-type
stockpiles (stat block +0x64) but not their aggregate twin, the u16
at block +0x5E, whose invariant is +0x5E == sum of the eight. Every
cheat-fed send decremented the aggregate from 0, so it underflowed
(Jerry's sessions reached -572): the HUD sword counter
(FUN_00146b98: (short)+0x5A + (ushort)+0x5E, clamped to a literal
1000) saturated at 1000, and the production gate — reading the
counter unsigned against the 1000 cap — stayed shut after the
cheat was disabled, soft-locking building. The tick now also pins
block +0x5E := 8 * stockpile_amount in the same branch (2-byte
store only; +0x5C/+0x5D are flag bytes). With the cheat on, the
aggregate always equals the pinned stockpiles; on disable, the
last tick leaves it equal to the real total and the game's own
bookkeeping tracks from there.

## #27 - Fixed -6 dB mix headroom (AUDIO_HEADROOM)

The mixer summed guest ports at full scale and hard-clipped the
result, so stacked sounds slammed into the ceiling. Following
iopenmohaa-ps3's hardware-validated PS3A_MIX_HEADROOM,
audio_mix_one_block now scales the finished mix by a master gain
before the AUDIO_MIX_NORM / clip stage. The gain comes from
AUDIO_HEADROOM, default 0.5 (-6 dB); 1.0 skips the multiply and
the mix is bit-exact the old way. Fork d3f0347a. A/B proof on
paired driven captures: the title-to-menu beep peaked at 0.0757
(control) vs 0.0379 with headroom - the same event at exactly
half amplitude.

## #28 - Mixer concealment stack (AUDIO_CONCEAL, default off)

When the guest audio producer falls behind under load, the mixer
used to play the unwritten ring block as a silent hole - heard as
chopping and crackle. Ported from whompay/inFAMOUSreborn's
vendored cellAudio.c (same runtime lineage; their issue #60 is
the same complaint), adapted to this fork's POSIX mixer and gated
behind AUDIO_CONCEAL=1 (default OFF pending a heavy-session
listening verdict): elastic consumption (a bounded wait,
AUDIO_ELASTIC_US default 6000 us, for a late block from a
recently-playing port when the device queue has slack); sentinel
marking of consumed blocks (-0.0f) so unwritten blocks are
distinguishable from written silence, with stall bounding
(AUDIO_STALL_MAX) and a self-protection give-up that restores
plain behaviour if holes persist pathologically; and PLC -
waveform-matched (WSOLA) repeat concealment of missing blocks
with crossfades, AUDIO_PLC_MODE default 3. Event counters print
under AUDIO_RATE=1. AUDIO_STRETCH was deliberately not ported:
it rides a host-FIFO design that conflicts with this fork's
deadline pacing + backlog drop/resync (885b5bab). Fork dd777aee.
Driven verification: machinery live in the stats lines, zero
false triggers on a quiet menu workload.

## #29 - Denormal flush (DAZ/FTZ) around lifted SPU runs (SPU_FTZ)

The SPU has no denormal numbers; x86 hosts pay a microcode
assist when lifted SPU arithmetic produces or consumes them,
and denormal-heavy guest code (the MultiStream synth) made
audio replies late in Soulbrandt's title. The fork already
bracketed every lifted run with a float-mode enter/leave pair
(the arm64 FPCR path); on x86-64 that pair now also saves the
caller's MXCSR, sets FTZ|DAZ for the run, and restores it at
the single epilogue (halts included). SPU_FTZ=0 opts out;
rounding is untouched and SPU_SCALAR_HELPERS builds are
excluded. Semantics argument: the SPU flushes denormals by
construction, so lifted results are identical - only the
host's assist cost changes. Fork b6b7c925. Driven verification:
boot to frame 1680 at 1920x1080; a menu capture contained
exactly the two driven beeps.

## #30 - Quadword shift helpers: per-word formulation

spu_shlqbii / spu_rotqbii and their bit-form siblings assembled
two 64-bit halves, shifted, and disassembled - per call, in
helpers every lifted SPU job uses constantly. Ported from
haze-recomp (3e97896): per-word shifts the host can vectorize;
the bit forms delegate or wrap a shared right-shift core.
Correctness gate: a standalone old-vs-new sweep - 20,151
vectors x the full shift ranges = 10,156,104 comparisons, 0
mismatches - and the committed bodies are token-identical to
the sweep-tested text. Fork 0691abed.

## #31 - SDL audio buffer 1024 frames (AUDIO_SDL_FRAMES)

The mixer requested a 256-frame SDL buffer (5.3 ms) - on
PipeWire desktops Soulbrandt measured that buffer running
short 12-14% of the time under load, and never at 1024. The
device request is now 1024 frames by default, env
AUDIO_SDL_FRAMES (clamped 256..4096); our mixer's deadline
pacing and drop/resync stay in charge of timing. The boot log
confirms the grant ("SDL device: freq=48000 ch=2
samples=1024"). Fork d7430289.

## #32 - Save states (experimental, branch muse/save-states)

F5 saves the current match to ~/tools/cc-recomp/saves/slot1.ccsave,
F9 loads it. Glue module applied by
recomp/scripts/patch_main_savestate.py; fork a9ec9227 (backend
hooks + ps3_rsx_flush_guest_caches) and 18ded31d (engine cache
invalidate). Sparse guest-RAM snapshot: header (magic CCSV0001,
mission id, mode, state, timestamp) + nonzero 4 KiB pages over
guest [0, 0x20000000); load validates the whole file before
touching memory, flushes RSX caches, reconciles and copies pages
back. Save is pause-only (state 14; state-13 captures tear).
Restores are byte-exact, same-session and cross-boot - but
resumption is not yet sound in-glue: in-place and title loads
leave the sim frozen, cross-instance loads into a paused match
collapse it, while the external SIGSTOP prototype's leg-4 flow
(same format) resumes and plays. Full evidence and the
stop-the-world analysis: notes/051-save-states.md.
