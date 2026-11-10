# 054 — Ground shadows: point sprites and G8B8 byte order

## Report

Heavy campaign session (mission 34, ~16 min): sprites "turn into black
dots" — the blue snake effect became a trail of dots and back — and black
marks linger on the ground after missile impacts. Both were worse the
busier the field.

## What the game does

`RSX_PRIM_STATS=1` (new, ps3recomp `fix/point-sprites`) showed one POINTS
draw per frame, 90–1,150 points, with POINT_PARAMS_ENABLE = 1 and
POINT_SPRITE_CONTROL = 0x103 (sprites on, unit 0 replaced). The pass dump
places it: it clears and renders a **384×384 ground-shadow map**
(0x0DEF0000), which the main scene pass samples as its t1. Every object —
rock tiles, flying rocks, the snake's segments, missiles — stamps a shadow
sprite from a 256×256 G8B8 atlas (uv = pointcoord × c1.x + per-vertex
cell offset).

## What we did wrong

1. The Vulkan backend never wrote PointSize or replaced the sprite
   texcoord: each stamp was one texel of the shadow map, magnified ~10×
   onto the 4K scene target — **the black dots**, under everything that
   moves, so they trace the snake and linger where missiles were.
2. With sizes fixed, every stamp was a flat square: the atlas lookup read
   the wrong byte of the G8B8 texel. Swapping the two bytes
   (`PS3RECOMP_G8B8_SWAP=1`) gives the intended soft round blobs.

Experiments (headless, scripted Battle, shadow map dumped): point-
coordinate debug output shows the correct 0..1 gradient per point; the
swap turns the squares into soft discs; the final frame has neither dots
nor squares. Frame rate unchanged (~88 fps in the scripted battle).

## Changes

- ps3recomp `fix/point-sprites` (bb01b5c7): point size + sprites in the
  Vulkan engine (PointCoord via a SPIR-V decoration rewrite), the G8B8
  option, the primitive stats. `PS3RECOMP_ENG_POINTS=0` restores the old
  rendering.
- `recomp/main.cpp`: Comet Crash sets `PS3RECOMP_G8B8_SWAP=1` at startup
  (overridable). The runtime default stays as it was until other titles
  using G8B8 are checked.

## Open

- Confirm on the real GPU in a heavy session (the report's conditions).
- The slowdown in busy scenes is a separate question (vertex load reached
  ~195k triangles' worth of vertices per frame in the scripted battle at its
  peak); this fix does not address it.
