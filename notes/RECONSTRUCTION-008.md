# Reconstruction notes 008 — RPCS3 as the Rosetta stone; the render chain, named

Date: 2026-11-05.

Jerry's suggestion: use RPCS3's source to see how PS3 library calls
are interpreted and adapted to native APIs. It paid off immediately,
twice over.

## FNID completion from RPCS3's HLE tables

RPCS3 implements the PS3 libraries as HLE modules registered *by
name*; its sources (sparse clone at `~/tools/rpcs3-ref`, Emu/Cell +
Emu/RSX) are therefore an authoritative name dictionary.
`scripts/rpcs3_candidates.txt` (724 names mined from the module
registration macros) now feeds `scripts/match_fnids.py`, and the
FNID hash does the verification — a candidate only sticks when its
SHA-1-derived hash equals the binary's import hash.

Result: **170/195 imports named** (was 134). Completed libraries:

- **cellGcmSys 36/36** — incl. `_cellGcmSetFlipCommand`,
  `_cellGcmSetFlipCommandWithWaitLabel`, `cellGcmSetDisplayBuffer`,
  cursor functions, report/zcull calls.
- **cellSpurs 22/22**, **cellResc 17/17**, **cellRtc 3/3**,
  **cellNetCtl 4/4**.

Remaining 25: cellSysutil 10, sceNp 5, sysPrxForUser 4,
cellVideoExportUtility 3, sceNpCommerce2 1, sys_io 2. (The sysutil
remainder lives in RPCS3 sub-module files not yet mined; PSN-adjacent
names stay low priority per the port boundary.)

How RPCS3 itself adapts these calls, for the port's benefit:
`cellGcmSys` HLE functions write RSX command-buffer state, and the
RSX thread translates command streams to Vulkan/OpenGL on the host.
The native port skips the command-buffer middle: the game's PSGL
calls become desktop-GL calls directly (notes 007), but RPCS3's
per-function implementations are the reference for what each
cellGcmSys call *means* when re-implementing the game's present path.

## The render chain is traced end to end

With the flip imports named, the corpus answers the question that
was open since notes 005 (which function presents the frame):

```
game_loop
  → FUN_000d8278 / FUN_000ff6d0        (frame steps)
    → FUN_00078b3c                     (per-frame render step)
      → frame_present (FUN_00079800)   ← renamed
          calls cellGcmSetFlipMode, cellRescSetConvertAndFlip,
          cellRescSetWaitFlip, and
          → gcm_flip (FUN_000ac00c)    ← renamed
              calls _cellGcmSetFlipCommand
```

`main` also reaches `frame_present` (init/shutdown flips). So the
per-frame structure is: game_loop → … → render step → present
(Resc upscaling blit + flip, or the raw GCM flip). The draw calls
themselves live inside PSGL (statically linked) beneath the render
step — the functions between FUN_00078b3c and PSGL's entry points
are the next naming target, and they are what the native GL backend
will re-implement function-by-function.

## The shader side: Cg tooling references

The game ships compiled Cg shader binaries (`data/ps3/*.fpo/.vpo`,
RSX vertex/fragment programs). References for the port:

- The **PS3 OpenGraphics Toolkit** (open-source alternative to
  Nvidia's proprietary Cg toolkit for PS3) — the community route to
  producing/understanding RSX shader binaries without the
  proprietary toolkit. (The announcement thread itself blocked
  automated reading — 403, plus a spam mirror — so only its stated
  purpose is relied on here.)
- **RSXGL** (Phoronix, 2012): an OpenGL 3.1 implementation for the
  RSX built on nouveau reverse engineering + Mesa GLSL compilation —
  evidence the RSX program model maps cleanly onto desktop GLSL.
- RPCS3's RSX shader decompiler (in the sparse clone,
  `rpcs3/Emu/RSX/Program/`) translates RSX fragment/vertex programs
  to GLSL — the direct reference when the port needs the game's
  shaders as GLSL.

Port plan for shaders, unchanged in shape: the originals' *roles*
(font, gui_quad, etc. — the filenames say which) get hand-written
GLSL equivalents in the native renderer; byte-level translation
only if a specific effect demands it.
