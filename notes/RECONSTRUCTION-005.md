# Reconstruction notes 005 — audio live in the port; the hidden module mapped

Date: 2026-11-05.

## Native: the game makes its own sounds now

The extracted SFX bank (notes 004) plays in the native host:

- MP3 decoding via **minimp3** (CC0, vendored in
  `native/third_party/minimp3/`), instantiated in
  `native/src/mp3_decode.cpp`.
- `native/src/sfx_player.*`: clips decoded once at load from
  `COMET_SFX_DIR`, queued onto an SDL audio stream on trigger —
  the MultiStream one-shot role, minus the engine. Slots today:
  **Choose** (the bank's `item_choose` click, on menu confirms),
  **Build** (its build sound, on arena placement taps), **Laser**
  (loaded, untriggered until gameplay exists).
- Verified headless (dummy audio driver + `COMET_SFX_SELFTEST`):
  all three clips decode (44.1 kHz mono; 10,368 / 55,296 / 76,032
  samples) and queue; clean exit.
- A shutdown-order bug was caught in the process: `main` called
  `SDL_Quit()` before the player's destructor destroyed the stream,
  corrupting the heap inside SDL. `SfxPlayer::shutdown()` now runs
  before `SDL_Quit()`; the destructor remains as a backstop.

## The hidden module at 0x14F35C–0x157590 is mapped

The ~33 KB stretch Ghidra never analyzed (notes 004) now has
functions: a Capstone scan for prologues and post-return addresses
produced 37 candidate entries (`scripts/region_entries.txt`,
`scripts/CreateRegionFunctions.java`), resolving to **23 new
functions** — corpus total **867**, all decompiled. The region's
core is a single **16,500-byte function** at 0x14F35C, with several
2–3 KB companions: a substantial subsystem, identity still open.

Two corrections to notes 004's reading of the area:

- The function created at 0x154828 is a 48-byte wrapper, not the
  float-heavy body the decompiler first showed (it had wandered into
  neighboring code from a 1-byte stub body).
- The `"%slevel%d.map"` load at 0x1549D8 belongs to **FUN_00154888**
  (2,960 B; 18 KB decompiled), which calls the same snprintf-like
  helper and the same file-load routine (0xEE4D8) as
  `level_load_file` — a second level-file consumer, and the current
  prime suspect for the map parser. Reading it is next.

## Render step: narrowed, not named

Tracing `game_loop`'s render call through imports fails by design:
rendering runs through the statically linked PSGL, whose internal
cellGcmSys calls sit three-plus hops away, and only 18 of 36
cellGcmSys imports are FNID-named (no direct flip caller is visible
at all). `game_loop`'s two largest callees — **FUN_00140da8**
(18.7 KB decompiled, the largest function in the corpus) and
**FUN_000b8074** (11.8 KB) — are the standing suspects for the world
update/render steps. No renames were made on this evidence.

## Tooling fix

`ExportCorpus2.java` broke when its `function_strings` block was
removed (notes 004): the block had declared the `ReferenceManager`
the import-callers block uses. The declaration now lives in the
imports block; the exporter runs clean again.
