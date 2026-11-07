# Reconstruction notes 035 — Board stuck zoomed into a corner + missing HUD text: the GPU NV3089 blit vs antialiased surfaces

Date: 2026-11-07.

## Symptoms

In the ps3recomp build (Jerry's fork, `~/tools/cc-recomp`):

1. Once a mission loads, the board stays zoomed into a corner — the
   enemy-base corner, around grid (0..5, 0..5) — instead of settling
   into the whole-board overview after the intro sweep.
2. The in-game HUD shows its panels/icons but no text or numbers
   (no timer, no Thorium count).

Menus render fine, text included. Jerry's lead: the zoom appeared
alongside the fork's "performance" commits; he pointed at
`gcerkez/ps3recomp` around commit c8562f37. He was right.

## What is NOT the cause

- The game's own camera code. Guest-memory dumps of the composed
  matrices (buffers at corpus VAs 0x54DFA0 / 0x54DFE0, relocated by
  the loader) in the settled overview state: projection scale
  (0.096, 0.170), translation (-0.632, 0.832) → look-at centre
  (6.6, 7.6), ~21x18 cells visible — the whole 16x16 board IS in the
  frustum. Modelview is the pure tilt rotation (cos/sin 0.766/0.643).
  Follow flag, zoom float (0.01), distance selection (18 overview /
  11 follow, notes 026) all behave per the corpus.
- The per-title glue's env pokes: a fully vanilla run (no
  CC_FORCE_*) reproduces the symptom. (run.sh's
  `CC_FORCE_OVERVIEW` did default to 1 in the `~/tools/cc-recomp`
  copy, which separately stalls the start sequence by stomping the
  follow flag every present; its default is now 0, matching the repo
  copy. That was a confounder during diagnosis, not the cause.)
- An earlier hypothesis — the runtime's AA sample-grid model itself
  (forcing the grid to 1x via a temporary `PS3RECOMP_AA_GRID1`
  switch) — made the sweep framing look plausible by shrinking the
  scene target to 1280x720, but HUD text stayed missing. Dead end;
  the experiment was reverted. The grid model is upstream behaviour
  (f752e40f, "rsx: antialiased surfaces at sample resolution") and
  the rasterization/viewport side of it is self-consistent
  ([RSX_VP] log: scene target 2560x1440, guest viewport
  trans=(640,360) scale=(640,360) → xf identity over the sample
  extent).

## Mechanism

Commit **c8562f37** ("video/rsx: GPU-native NV3089 blits, ...",
2026-11-06, the grind's performance pass) added
`rsx_draw_engine_blit()`: NV3089 IMAGE_IN blits whose source resolves
to an engine-rendered RGBA8 surface are executed as `vkCmdBlitImage`
instead of the previous guest-memory copy.

The game's scene target is 1280x720 logical with a 2x2 antialias
grid, so the engine holds it as a 2560x1440 surface. But NV3089 blit
geometry arrives in the guest's LOGICAL pixels (the caller in
`cellGcmSys.c` computes sx/sy/sw/sh from the 3089 registers, ds_dx /
dt_dy scaling included). The fast path fed those numbers straight
to the blit as sample-grid coordinates:

- A blit meant to copy the whole logical scene (1280x720) copied the
  top-left 1280x720 SAMPLES of the 2560x1440 grid — exactly one
  quadrant of the composed frame. The post chain then presented
  that crop as the full frame: a ~2x zoom into the north-west
  (enemy) quadrant. Pixel analysis of broken screenshots matches:
  visible world region x in [-4, 6.6], z in [-1.5, 7.6] — all 14
  turrets (x<=5) and the enemy base (3,2) visible, the player base
  (11,14) never.
- HUD glyph surfaces moved through the same path were likewise
  copied from the wrong region, so HUD text/numbers vanished while
  directly-drawn panels and icons survived.

## Evidence (A/B, same binary, driven headless via PAD_FILE)

- Stock build, `RSX_GPU_BLIT=0` (the fast path's own env gate):
  the title card ("Kreios Area 1", "Press X to start...") renders
  with text; after start, proj settles to (0.096, 0.170) and the
  frame shows the ENTIRE 16x16 board in its border, both bases,
  and a complete HUD — timer 00:05, hearts 25/25, Thorium 100
  (level 1 header[4]), win target 25 (header[5]).
- Same build with GPU blits enabled: quadrant zoom + empty HUD
  panels (runs 1-7 passim).

## Fix

`libs/video/rsx_draw_engine.c`, in `rsx_draw_engine_blit()`: decline
the GPU path (return 0 → the guest-memory copy runs, exactly as
before c8562f37) whenever the source or destination engine surface
is antialiased:

```c
if ((g.surfaces[si].aa_grid & 0xFFu) > 1u || (g.surfaces[si].aa_grid >> 8) > 1u)
    return 0;
```

and the same check for `g.surfaces[di]` after destination
resolution. `aa_grid` is `aa_sx | aa_sy << 8` on `eng_surface`
(0 = unspecified = treat as 1x). Non-AA blits — the pattern the
fast path was built for — keep the GPU path and its speedup.

Verified: rebuilt with the decline, NO env overrides, fresh run
(menu → campaign → join → fly-in with follow=1 set by the game →
title card → start → follow cleared by the game → overview): full
board, full HUD, identical composition to the blit-off A/B.

Status: the fork change is committed in `~/tools/ps3recomp` as
`b8e5cbb7` ("video/rsx: GPU NV3089 blit declines antialiased
surfaces", 2026-11-07, at Jerry's direction; local commit, not
pushed); logged as entry 7 in `~/tools/cc-recomp/RUNTIME_PATCHES.md`.
run.sh's `CC_FORCE_OVERVIEW` default is now 0 in the tools copy.

## Possible refinement (not done)

For a non-AA destination, scaling the source rectangle by the aa
grid would let vkCmdBlitImage double as the AA resolve and keep
even these blits on the GPU. The decline is the conservative,
pre-regression behaviour; the refinement needs its own A/B against
titles that resolve-by-blit deliberately.

## Also observed (separate, pre-existing)

- State 13 runs at ~20-35 flips/s headless and the mission clock
  runs slow (~1/3), suggesting game time tied to frames at an
  assumed 60 Hz; the intro sweep takes minutes of wall time. Not
  part of this fix.
- The post-join intro phase varies between runs with identical
  inputs (title-card flow vs enemy-base ambient loop) — timing
  sensitive; not investigated here.
