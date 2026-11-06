# Reconstruction notes 032 — ps3recomp bring-up, day one

Date: 2026-11-06. Jerry approved the ps3recomp route
with a specific target: a Vulkan/OpenGL Linux executable
he can try. Project: ~/tools/cc-recomp on JerryDesktop
(ps3recomp cloned at ~/tools/ps3recomp, HEAD of
2026-11-06). Toolchain in the iloader distrobox (GCC 16,
ninja and vulkan-headers installed into the box).

## Pipeline (all ps3recomp's own tools)

1. ppu_loader.py on our decrypted EBOOT.ELF: 851 OPD
   functions, import map identical to the corpus's.
2. find_functions.py --seed-json: 2,607 functions
   (corpus says 2,647 — 98.5% agreement, independent
   method).
3. ppu_lifter.py --hle-stubs: 4,867 functions lifted in
   ~7 s, 47 MB of C++ (two chunks).
4. Template project (CometCrashRecomp), configured with
   -DPS3RECOMP_RSX_VULKAN=ON, HLE NID table generated for
   every module. Runtime + game built in ~5.5 min:
   build/CometCrashRecomp, a 20.7 MB native Linux
   executable on the Vulkan RSX backend.

## SPU registration — the string-island fix

Configure-time SPU generation failed on both embedded
images, and an unregistered image is DROPPED at async
dispatch, which deadlocked the boot (a PPU thread waits
forever on an event flag the audio SPU must set).
- Small image (3,124 B): lifted, incomplete coverage —
  registers with build_spu_workloads --allow-partial.
- Big image (95,636 B, the MultiStream engine): the
  lifter emitted 473 ".word" placeholders, all inside
  ONE data island at LS 0x171D0-0x1A1CC (printf format
  strings — readable ASCII — plus decode tables) that
  function discovery had swept into 49 pseudo-functions.
  Re-lifted manually: code.bin --base 0x3000 (its .text
  LOAD vaddr; the small image's is 0x80), functions =
  the discovered set minus the 49, --symbol-prefix
  matching the registration (spu_0001_at_001C0B80_ /
  spu_0000_at_00175D00_). Result: 1,152 functions, zero
  unsupported. Both images registered 2/2 via
  --images --skip-lift --allow-partial. The island bytes
  stay in the LS image as data, which is all they are.

## VFS layout

The game addresses files two ways: /dev_bdvd/PS3_GAME/
USRDIR/... and bare USRDIR/... . PS3_VFS_ROOT=
~/tools/cc-recomp/vfs with vfs/PS3_GAME/USRDIR/data a
MERGED directory of symlinks: full-package entries,
with the 1.10 patch tree's files overlaid on top (the
language*.utf files exist ONLY in the patch tree; the
full package's data dir lacks them). vfs/USRDIR ->
PS3_GAME/USRDIR covers the second shape. The two
Japanese font probes (wmd_font_readable_j*.fnt) exist
in neither tree (JP SKU files) and are tolerated.

## Boot state after day one

Boots deep: CRT startup in the corpus's documented
order, all sysmodule loads, SPU init, Vulkan device on
the RX 7900 XTX (RADV) under Xvfb, cellPad sees the
Xbox Elite controller, GUI + font shaders load from
data/ps3/, settings.dat reads, first RSX DRAW_ARRAYS
issued (GUI quads), label fences advance. With the
language tables missing it then idles (async loader
never completes). With them present it proceeds — and
hits the day's remaining wall:

1. "[cellGcmSys] FIFO get=0x02300000 has no IO mapping
   -- resyncing to put=0x00000000": the RSX command
   FIFO's get pointer lands somewhere the runtime's IO
   map doesn't cover. First hard divergence; the GCM
   memory/IO mapping is the next investigation.
2. Immediately after, MultiStream prints "SAFE GUARD
   MEMORY HAS BEEN CORRUPTED" (start+end guards) on
   every check — its buffers are being trampled or
   never initialized in this environment.
3. The process then segfaults inside the game's debug
   FPS overlay (game_loop -> debug_fpsOverlay 0x101258
   -> quad path 0x140600 -> FUN_00081988's matrix copy)
   reading guest EA 0xDFFEFFFC with the guest SP inside
   the runtime's callback-stack window (top 16 MB of
   the 0xD0000000 stack region). Ruled out by direct
   tests: the inline-load sign path (PPU_RWATCH run
   crashed identically) and unlifted-SPU branches
   (SPU_INTERP_UNLIFTED=1 changed nothing; the SPU
   never branched into the excluded island). The
   corruption in (2) likely feeds the bad pointer in
   (3); both begin at the same boot phase, when the
   game starts using its large sys_memory pools
   (0x10000000 + 0x11000000, 37 MB).

Status: a real, bootable Linux/Vulkan executable of
the actual game — not yet at the title screen. The
hand port and corpus remain the understanding layer;
this is the oracle track, one divergence at a time.
Run recipe: ~/tools/cc-recomp/run.sh (distrobox iloader;
PS3_VFS_ROOT + EBOOT.ELF argument).
