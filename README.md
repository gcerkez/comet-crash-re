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
| Tooling | [scripts/](scripts/) | The exact Ghidra export script used to produce the corpus, so every artifact here is reproducible. |

## Reconstruction progress

- ✅ Binary decrypted and verified (PPC64 ELF, entry via OPD at 0x1F2B38)
- ✅ Full auto-analysis + decompile: **843/843 functions, 0 failures**
- ✅ First-pass findings: leaked source files (`main.c`, `arenaGraphics.cpp`,
  `ObjectGroup.cpp`, `sound/engine.cpp`, `local_text.cpp`,
  `fontMapFormat.cpp`), the `gCometEnv` global, weapon/unit model taxonomy,
  the YouTube upload feature's API key string
- ✅ Import map decoded from `.lib.stub`: 20 libraries, 195 functions (incl. input = `sys_io`, 6 functions)
- ✅ Native host milestone 1: builds and runs — SDL3 window, unified keyboard/mouse + hot-plug gamepad input, unit-tested mapper
- ⬜ Startup chain walked from the entry OPD to `main`
- ✅ FNID pass: **134/195 imports named** and labeled in Ghidra; corpus v2 exported (notes/RECONSTRUCTION-002.md)
- ✅ Input seam found: `FUN_000dae30` (pad init) and `FUN_00110738` (per-frame pad sampler); original proven pad-only — no built-in keyboard/mouse
- ✅ Module map v1: corpus split into per-module files under decompiled/by-module/ (MultiStream 66, PSGL 21, local_text 9, C runtime 17, rest unassigned)
- ✅ Language tables extracted: 265 English strings incl. all menu text (analysis/language/)
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
