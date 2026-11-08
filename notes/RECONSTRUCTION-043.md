# 043 — The gameplay view fault: Thorium gauge strip + the bottom-left lock

Two visible defects in real play sessions at 1920×1080, menus and
title cards unaffected. Both are closed here; they were unrelated.

## A. The atlas strip — the Thorium gauge escaping its frame

A horizontal strip of atlas icons stretched across the screen in
Battle with the cheats on. Identified as the player panel's Thorium
gauge fill overflowing its frame: the spill length scales with the
pinned Thorium value (100000: a full-width bar plus flags; 9999:
~13 icons; 5000: L1/R1 plus ~5 flags). A gauge/UV fault the cheat's
out-of-range values exposed; natural values keep the fill in frame.
Pin capped at 1000 (nothing in game costs more) — confirmed gone in
real play, corroborated by driven frames (panel reads 1000, no
strip). Latent: the spill threshold lies between 1000 and 5000, so
a legitimate balance in the low thousands might spill slightly —
untested, parked.

## B. The view locked to the bottom-left, with a seam

Symptom: in live matches (and behind the battle-end/results
screens), the arena view was stuck zoomed into the board's
bottom-left corner; at x ≈ 0.69 a black border split the frame from
a second, disjoint magnified view. At every Thorium pin value.

Eliminated, in order:

- The glue camera override: `comet_crash_camera_tick()` is
  read-only unless its envs are set (they were not); a
  forced-overview drive still showed the fault.
- The guest viewport: [RSX_VP] sampling showed one constant,
  correct transform for the scene surface all session.
- GPU/present specifics: an Xvfb/llvmpipe drive reproduced the
  fault identically, so it is an engine composition fault.
- The scene render itself: an RSX_ENGINE_PASS_DUMP drive's pass-001
  target (the 3840×2160 AA scene surface) contains the WHOLE board
  rendered correctly, while the display surface (pass 008 target)
  already contains the exact corruption. The fault sits between
  the scene surface and the display surface. (Pass dumps only dump
  targets; the resolved texture is source-only, so the localization
  is "last good target vs first bad target".)

Bisect: RSX_GPU_BLIT=0 (the CPU fallback for NV3089 blits) renders
the whole board perfectly. The GPU blit path is the culprit.

## The guest's resolve, and what the engine did with it

Per frame the guest resolves the AA scene into the 1920×1080
texture the bloom chain and the display pass sample, as **twelve
NV3089 blits**: a 4×3 tile grid, each out 512×512, in 1024×1024
samples, ds=dt=2.0, tile position encoded in the src/dst OFFSETS
(src pitch 16384, dst pitch 7680; the handler clips the edge tiles
to 384 wide / 56 high via the guest's per-tile clip).

Coherence-log ground truth before the fix: every tile blit resolved
to its OWN destination surface at the tile's offset, rect
(0,0,tile-size) — twelve fragments, no shared surface. The texture
sampler resolves the buffer's base offset, so the display sampled
effectively one fragment — the scene's bottom-left tile — stretched
across the frame, with a second fragment showing through right of
the seam.

Root cause: when `rsx_draw_engine_blit` finds no destination
surface, it created one sized (dx+dw, dy+dh) — one tile's rect — at
the tile's offset. `eng_surface_at` containment (start point must
satisfy row < h and byte-in-row < w·bpp at matching pitch) then
rejected every sibling tile, so each created its own fragment.

## Fix (fork 4a6101c4)

Size a newly created blit destination from the destination pitch —
the pitch names the buffer's row width (7680/4 = 1920) — and, when
that width is the frame's width, give it the frame's height as
well. The first tile creates the whole 1920×1080 surface; every
later tile resolves into it at its proper rect. Narrower buffers
keep the previous rect sizing.

## Verification (A/B discipline)

- Coherence log after the fix: all twelve tiles land in ONE
  surface at exact rects — columns at x = 0/512/1024/1536 (edge
  column 384 wide), bands at y = 0/512/1024 (edge band 56 high).
- Pass-dump with the GPU path on: the presented frame shows the
  whole board (bases, full rock layout, ship, correct HUD) —
  matching the RSX_GPU_BLIT=0 reference.
- Real GPU (:0): a driven match's battle-end screen is clean, and
  the 60 s RSX_PRESENT protocol reads 71.0 fps — identical to the
  entry-16 build. The GPU fast path and its speed are retained.
