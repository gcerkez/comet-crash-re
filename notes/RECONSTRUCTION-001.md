# Reconstruction notes 001 — the corpus and first findings

Date: 2026-11-05. Source: Ghidra 12.2 auto-analysis + full decompile of the
decrypted `EBOOT.ELF` (see PROVENANCE.md). All addresses are image addresses
(image base 0, as loaded by Ghidra).

## Corpus

| | |
|---|---|
| Functions found | **843** (all decompiled cleanly, 0 failures) |
| Named symbols | **0** — the binary is fully stripped; every name is a Ghidra `FUN_<address>` placeholder |
| Defined strings | **2,005** |
| Decompiled output | `decompiled/decompiled.c`, ~2.3 MB of C pseudocode |
| Code region | `0x00010230–0x0017305F` (~1.45 MB executable) |
| BSS | extends to `0x00AAAE17` (~8.3 MB uninitialized — large static game buffers) |

Largest functions (first reading targets): `FUN_0010d390` (13,220 B),
`FUN_00014944` (10,228 B), `FUN_000837e8` (7,732 B), `FUN_0007b47c`
(7,332 B), `FUN_000c482c` (6,988 B).

**Caveat:** 843 functions is almost certainly an *undercount* of the true
function total. In a stripped PPC64 binary, Ghidra only finds functions it
can reach from the entry chain and cross-references; unreferenced or
indirectly-called code (vtable targets, OPD-referenced functions) can hide.
Treat the corpus as v1 and expect the count to grow with OPD/vtable passes.

## Entry point is an OPD, not code

The ELF header entry is `0x1F2B38`, which sits in a read-only *data* region,
not in the code section. That's expected for PPC64 ELFv1: the entry points
at an **official procedure descriptor** — a {code address, TOC base} pair —
for the real start function. Any by-hand navigation should dereference the
OPD first. (Ghidra's loader handled this during analysis.)

## Source-file remnants (from assert/debug strings)

The binary leaks several of the game's original source file names:

| File | Subsystem it implies |
|---|---|
| `main.c` | entry/bootstrap |
| `arenaGraphics.cpp` | the arena (playfield) renderer |
| `ObjectGroup.cpp` | scene/object grouping |
| `fontMapFormat.cpp` | bitmap font system |
| `local_text.cpp` | localization (matches the `language*.utf` files in the game data) |
| `sound/engine.cpp` | the sound engine |
| `--GameSpot.c` | unknown; possibly a leftover/renamed module |

It also leaks a build-machine path from the C library:
`/home/kanee/svnwork/ps3-svn/svn/sys/trunk/dinkumware/...` — the game was
built against the Dinkumware C library (the ProDG/SN Systems toolchain
library), from an SVN checkout on a developer machine (user `kanee`).

## String-guided subsystem map

Evidence from `analysis/strings.tsv` (2,005 strings):

- **Weapons/structures** — model paths under `models/care/weapons/`:
  `structTurretStand.obj`, `structTurretBarrel.obj`, `structLaserStand.obj`,
  `structLaserBarrel.obj`, `structMissileStand.obj`,
  `structMissileBarrels.obj`, `structBasePlat.obj`. The tower set is built
  from composited stand/barrel meshes.
- **Units/agents** — `models/care/agents/agentAdv_Laser.obj`, and the unit
  name `UNIT-5-LASER BOSS`: the campaign has named boss units.
- **Sound** — `sound/engine.cpp` plus referenced MP3s such as
  `251156_Laser_Shot_Thin.mp3`, `171588_MOD_laser_startup.mp3`.
- **Video/online** — `CometCrashVR.mp4` (an attract/intro movie) and
  `ytapi-Pelfast-CometCrash-a3hatuee-0`: a YouTube API developer key. The
  PS3 release could record and upload gameplay video to YouTube.
- **Debug UI** — window-title format strings `Comet Crash %uP`,
  `Comet Crash %uP %s %s %u` (players, mode, level…) from the original
  debug/dev overlay.
- **Core globals** — `gCometEnv` (the global game-environment object) and
  `mCometObjPath` remain the two most valuable anchors: most game state
  likely hangs off `gCometEnv`.

## Next steps

1. Dereference the entry OPD and walk the startup chain from the real
   `_start`/`main` to `gCometEnv` initialization.
2. Resolve the PS3 SDK import stubs (`.lib.stub` FNID table) against a
   public FNID database so calls into `libsysutil`/graphics/audio libraries
   get real names — this will rename a large fraction of the call graph.
3. Cluster functions by which strings/data they reference (weapons, sound,
   text) to draw the subsystem boundaries, then start renaming in the
   Ghidra project and mirroring cleaned code into `reconstruction/`.
4. Look at the biggest functions first (table above) — in a game this size
   they are usually the per-frame update/dispatch hubs.
