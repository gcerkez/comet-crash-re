# 🎮 Comet Crash — Reconstruction Project

![Comet Crash icon](assets/icon0.png)

Sony published Pelfast's **Comet Crash** for PlayStation 3 via PSN in 2009.
It is a tower-defense game with an offensive twist: alongside building and
upgrading towers (turrets, lasers, missiles) to stop waves of alien craft
from reaching your base, you build up a comet of your own units and launch
counterattacks to destroy the enemy base first. It shipped with a 28-stage
campaign, offline co-op, and competitive multiplayer — and, very much of its
era, a feature to record gameplay and upload it to YouTube.

This repository is a **decompilation-style reconstruction project** for that
game, modeled on the community reconstruction projects cataloged in
[awesome-rea-games](https://github.com/HaozheH3/awesome-rea-games): start
from the shipped binary, recover a complete decompiled corpus, then
progressively identify, name, and document the game's logic until the
reconstructed code reads like the program that produced it.

**Private research project for a game I own. It contains analysis output
(decompiled pseudocode, inventories, notes) — not the game's assets or the
game binary itself.**

## This project

| Component | Resources | What it is |
|---|---|---|
| Decrypted executable | [notes/PROVENANCE.md](notes/PROVENANCE.md) | How the NPDRM SELF (`EBOOT.BIN`) was decrypted with scetool + the title's RAP into a plain PPC64 ELF — the artifact everything else is built from. |
| Analysis corpus | [analysis/](analysis/) | Machine-generated inventories from the binary: 843 functions, 2,005 strings, memory map, plus the REA attempt records. |
| Decompiled code | [decompiled/decompiled.c](decompiled/decompiled.c) | Ghidra decompilation of **all 843 functions** (~2.3 MB of C pseudocode). Names are `FUN_<address>` placeholders; this is reconstructed logic, not original source. |
| Reconstruction notes | [notes/](notes/) | Human/AI findings as identification proceeds — start with [RECONSTRUCTION-001](notes/RECONSTRUCTION-001.md) (corpus tour, leaked source-file names, string-guided subsystem map). |
| Cleaned reconstruction | [reconstruction/](reconstruction/) | Renamed, re-typed, commented code for subsystems we actually understand. Empty for now; grows from the notes. |
| Native host | [native/](native/) | The C++17 + SDL3 app the reconstruction grows into. Milestone 1: window + frame loop + unified keyboard/mouse/gamepad input (the native stand-in for the original's `sys_io` pad sampling). |
| Port notes | [notes/NATIVE-PORT-001.md](notes/NATIVE-PORT-001.md) | The module breakdown of the original, the decoded import map (20 libraries / 195 functions), the PS3→native service map, and the port roadmap. |
| Static recompilation | [recomp/](recomp/) | Full, playable PC static recompilation (`CometCrashRecomp`) targeting Linux x64/Vulkan via ps3recomp with native SPU multi-threading and corrected 720p scaling. |
| Tooling | [scripts/](scripts/) | The exact Ghidra export script used to produce the corpus, so every artifact here is reproducible. |

## Reconstruction progress

- ✅ Binary decrypted and verified (PPC64 ELF, entry via OPD at 0x1F2B38)
- ✅ Full auto-analysis + decompile: 843/843 functions, 0 failures; **867** after discovering a missed 33 KB module (notes/RECONSTRUCTION-005.md)
- ✅ First-pass findings: leaked source files (`main.c`, `arenaGraphics.cpp`,
  `ObjectGroup.cpp`, `sound/engine.cpp`, `local_text.cpp`,
  `fontMapFormat.cpp`), the `gCometEnv` global, weapon/unit model taxonomy,
  the YouTube upload feature's API key string
- ✅ Import map decoded from `.lib.stub`: 20 libraries, 195 functions (incl. input = `sys_io`, 6 functions)
- ✅ Native host milestone 1: builds and runs — SDL3 window, unified keyboard/mouse + hot-plug gamepad input, unit-tested mapper
- ✅ Startup chain walked: _entry → crt_startup → **main** (0xDAE30), all named in Ghidra; 77 import thunks renamed to SDK names (notes/RECONSTRUCTION-003.md)
- ✅ FNID pass: **170/195 imports named** and labeled in Ghidra (cellGcmSys/cellSpurs/cellResc complete via RPCS3 HLE tables); corpus exported (notes/RECONSTRUCTION-002.md, RECONSTRUCTION-008.md)
- ✅ Input seam named: `pads_update` (0x111278) → `pads_sample` (0x110738, per-frame cellPadGetData); original proven pad-only — no built-in keyboard/mouse
- ✅ Module map v1: corpus split into per-module files under decompiled/by-module/ (MultiStream 66, PSGL 21, local_text 9, C runtime 17, rest unassigned)
- ✅ Language tables extracted: 265 English strings incl. all menu text (analysis/language/)
- ✅ Data formats opened: `.pak` archive cracked + extracted (16 MP3 SFX, scripts/extract_pak.py); `.map` format cracked from its parser (level_parse) and verified byte-exact on all 12 maps — native parser implements it (notes/RECONSTRUCTION-006.md)
- ✅ Frame chain named (main → game_run → game_loop → frame_input → pads_update)
- ✅ Native host plays the game's own SFX (extracted MP3 bank via minimp3 + SDL audio)
- ✅ Native host renders the game's own OBJ models in a projected 3D arena scene (COMET_MODEL; notes/RECONSTRUCTION-007.md)
- ✅ The render chain is traced to the present step: game_loop → render step → frame_present (cellResc convert+flip) → gcm_flip (notes/RECONSTRUCTION-008.md)
- ✅ Level semantics completed with cross-decomp validation (Stylish-Dark, different build): type-0x0B record = arena extent marker; secondary records = filtered spawn-event list; all 49 unique embedded levels extracted (notes/RECONSTRUCTION-009.md)
- ✅ **The arena is playable**: native gameplay core (arena_sim) flies the ship on the level grid, spawns the level's own scheduled events, combat + base defense, HUD — verified headlessly on level61 (notes/RECONSTRUCTION-010.md)
- ✅ Cross-resource audit done; **placement cracked** (primary records sit at raw[2]/raw[3]) and the recovered **construction model** implemented: dormant structures activated by the level script, factory production, funded player builds (notes/RECONSTRUCTION-011.md)
- ✅ **Mission setup recovered** from the decomp resources: per-kind initial levels + the raw[1] settlement rule, factories = kinds 26/28 with stockpile-and-deploy production, script events as timed construction (notes/RECONSTRUCTION-012.md)
- ✅ Kind identities mapped (20 Base … 28 Special Ops; units 40–47); **Thorium economy from meteors, ship health/destruction/reset, and the offensive win condition** implemented (notes/RECONSTRUCTION-013.md)
- ✅ **Rock economy corrected to the real mechanic**: data-driven flying rocks (selectors 20–22, per-rock payouts), ship grab-and-drag, base markers decoded, BFS base-to-base routing with unblockable-path construction (notes/RECONSTRUCTION-014.md)
- ✅ **Presentation pass**: fixed top-down camera (the map never moves), DDS texture pipeline, the game's own artwork in the scene — textured fighter/scout models, structure billboards, comet-surface ground (notes/RECONSTRUCTION-016.md)
- ✅ **Full-asset rewire**: the complete base-game package recovered (all 44 registry models); the scene composes the named meshes — comet terrains, articulated structures on platforms, geode rocks (notes/RECONSTRUCTION-017.md, notes/RECONSTRUCTION-018.md)
- ✅ **Menu-driven data flow + the game's own font**: levels selected in the menus load from the data root; all text in the game's BMFont (Proxima Nova). Camera/HUD geometry still estimates — the code is the definition of truth; its consumers were located in the uncarved setup hub (notes/RECONSTRUCTION-019.md)
- ⬜ Subsystems clustered and named; first modules in `reconstruction/`

## The target

| | |
|---|---|
| Title | Comet Crash |
| Title ID | NPUB30061 (US PSN) |
| Content ID | UP2009-NPUB30061_00-PELFASTCC0000020 |
| Binary | `EBOOT.BIN` → decrypted `EBOOT.ELF` |
| Format | ELF64, big-endian, PowerPC64 (Cell PPU / LV2), statically linked, stripped |
| Size | 2,817,600 bytes decrypted |
| Entry point | 0x1F2B38 (an OPD — see notes 001) |

## Pipeline

1. **Decrypt** (done): scetool → `EBOOT.ELF`. Full chain in
   [notes/PROVENANCE.md](notes/PROVENANCE.md).
2. **Analyze** (done, v1 corpus): Ghidra 12.2 headless, language
   `PowerPC:BE:64:default`, auto-analysis, then a scripted export
   (`scripts/ExportReconstruction.java`).
3. **Reconstruct** (ongoing): read the corpus, name subsystems, write up
   the game's logic in `notes/`, promote understood code to
   `reconstruction/`.

### A note on REA

REA (Reverse Engineer Anything, `rea-agents` 6.7.0) is installed on the
analysis machine and registered with Antigravity, and its Ghidra engine is
the same one used here — but REA 6.7.0 **rejects this target at intake**
("unsupported ELF architecture") because its artifact layer does not support
PowerPC64/Cell LV2 ELFs, even with `--provider ghidra`. The corpus was
therefore produced by driving Ghidra headless directly. Details and the
exact error record are in [notes/REA.md](notes/REA.md).

## Repo layout

- `assets/` — the game's icon from the extracted package, for presentation.
- `analysis/` — machine-generated inventories (`functions.tsv`,
  `strings.tsv`, `summary.txt`) and the REA attempt records.
- `decompiled/` — the decompiled C corpus.
- `notes/` — provenance, REA findings, reconstruction write-ups.
- `reconstruction/` — cleaned-up reconstructed code (grows over time).
- `scripts/` — the scripts that produced the corpus.

Not in git (kept on the analysis machine): the decrypted `EBOOT.ELF`, the
Ghidra project database (`ghidra/`), and the RAP license file — see
`notes/PROVENANCE.md` for where they live and how they were obtained.

## Reproducing the analysis

On the analysis machine (JerryDesktop):

```bash
source ~/tools/env.sh   # JAVA_HOME (Temurin 25 for Ghidra 12.2), GHIDRA_INSTALL_DIR, Node 24
~/tools/ghidra_12.2_PUBLIC/support/analyzeHeadless ~/comet-crash-re/ghidra CometCrashRE \
  -import ~/Downloads/comet_crash_extracted/EBOOT.ELF \
  -processor PowerPC:BE:64:default -cspec default \
  -scriptPath ~/comet-crash-re/scripts \
  -postScript ExportReconstruction.java ~/comet-crash-re/analysis
```
