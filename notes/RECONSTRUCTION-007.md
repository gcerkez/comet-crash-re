# Reconstruction notes 007 — field profiles, models, and the render path

Date: 2026-11-05.

## .map field profiles (all 12 maps, 1,187 + 156 records)

- **Orientation records**: only bytes 0–23 are ever nonzero (the
  rest of the 56 is padding, confirmed corpus-wide). Types seen:
  9 (1,105 — the bulk), 26, 21, 23, 11, 10, 29, 24, 28. The u32 @20
  is 0 (955), 42 (201), or a flags-like value (0x01020000-ish, 25).
  Bytes 1–3 are data too (the parser's type-11 handling reads
  @2/@3). No position fields exist in these records.
- **Object records**: only bytes 0–8 nonzero. The float @0 spans
  7.19–795.58 (position-scale). Byte @6 is a small class (0, 1, 2);
  byte @7 carries 20–28 — matching the value set `level_parse`
  branches on, so @7 is the record's type for parser purposes; @8
  is used as well.
- **Header fields** (index: readings): [2] is two u16s — the high
  half runs 0–3 across maps (a world/planet index; low half 0x100);
  [3] ∈ {60…1200} and [4] ∈ {150…650} are the big round numbers
  (extents or starting resources — still ambiguous); [5] ∈ {25,30,50};
  [6] ∈ {9…23}; [7] as a float is exactly **180.0**; [8] ∈ {1000,1200}.
  Fields 11–18 hold dense non-round values (float pairs or hashes —
  open).

Positions for placed content are still not in the map file in any
obvious slot: they must be derived by the consumer (grid/index) or
live in the level records table (the 0x22C-byte records the
level-select code walks). Record consumers remain the next read.

## The models are real — and tiny

`USRDIR/data/models/care/` holds Wavefront `.obj` + `.mtl` files
with DDS textures. They are small: `fighter02.obj` is 16 vertices /
28 triangles, `scout02.obj` similar. These are the game's actual
shipped meshes for those units (low-poly with the detail carried by
textures: DDS in ABGR8888 and DXT flavors, 64×64 for units). The
native host now loads and renders them (below). DDS decoding is the
remaining step for textured rendering.

## The render path: PSGL → native

PSGL is Sony's OpenGL ES 1.x layer over cellGcmSys. The game links
it statically, so there is no call boundary to intercept — the port
replaces the whole layer, per the NATIVE-PORT-001 service map:

- **PSGL (ES 1.x fixed function)** → desktop **OpenGL** (compat
  profile is the near-1:1 analog) or the host's own projected
  renderer as an interim. The game's draw code (inside the corpus's
  game functions, e.g. the `game_loop` callees) calls PSGL entry
  points by address; those get identified and re-implemented
  function-by-function as the game logic is ported — the same
  treatment MultiStream got for audio (notes 005).
- **cellGcmSys** (36 imports, 18 named) has no native counterpart
  and needs none: it is what PSGL itself sat on. The flip/present
  step becomes SDL's window present.
- **cellResc** (upscaling) → the host window/renderer scaling.

Interim state (native milestone 6): the arena renders a projected
3D scene in the host — ground grid sized from the level header, the
game's own OBJ geometry lambert-shaded and painter-sorted, camera
orbiting — via SDL_RenderGeometry today, with the structure (scene
build → project → draw list) mirroring where the GL backend will
sit. Verified headless by driving the real binary into the arena
(`COMET_START_ARENA=1` demo script; the frame log shows the Arena
screen reached and the scene drawing for 140+ frames).
