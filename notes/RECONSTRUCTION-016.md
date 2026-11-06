# Reconstruction notes 016 — presentation pass: fixed camera, real artwork

Date: 2026-11-06. From Jerry's first hands-on run of the native
build, comparing it with a screenshot of the original: the view
must be top-down and the map must not move; the graphics must
be the game's own.

## What the original does (reference screenshot)

A fixed, steeply tilted view of the whole square arena; the
field never moves. Textured comet-surface ground, structures
and units sitting on it, status panels at the left.

## Asset reality check

Only two meshes ship as files — fighter02.obj (the ship) and
scout02.obj — each with a Blender .mtl naming a 64×64
uncompressed 32-bit DDS diffuse (fighter_DFTC04.dds,
scout_DFTC04.dds). Everything else exists as DDS artwork only:
structures/structBeacon, weapons/structBasePlat, structLaser,
structMissile, care/stunner, care/converter, cometGaseous (the
comet surface), agents/*. There is no ground texture file; the
comet surface is the cometGaseous art.

## Native milestone 12 — implemented

- **DDS decoder** (`dds_texture`): mask-driven uncompressed
  24/32-bit plus DXT1/3/5 block decoding; unit-tested with
  synthetic images, and verified live against the real files.
- **OBJ loader keeps UVs** now (vt indices per corner).
- **Fixed camera**: high, south of the field, steep tilt,
  framing the whole extent; no time input anywhere — the map
  is static. The ship and units keep their own yaw only.
- **Real artwork in the scene**: the ground is the cometGaseous
  texture tiled per cell with tint variation; the ship renders
  as fighter02 with its own texture at ~1.7 cells; units render
  as the textured scout model, side-tinted; structures and
  terrain rocks present their DDS artwork as standing
  billboards (kind → artwork mapping is cosmetic and named in
  buildSceneAssets; dormant sites sit low and dark, building
  ones pulse); bases get a coloured pad + the base-platform
  art.
- **HUD**: two short lines that always fit; the debug
  "LEVEL RECORDS" caption and the briefing text no longer
  overlay the arena.

ctest 6/6 (new dds_texture suite). Visual check is Jerry's —
the headless driver proves loading, not looks.
