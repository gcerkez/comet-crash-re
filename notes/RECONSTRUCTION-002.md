# Reconstruction notes 002 — imports named, corpus v2, module map v1

Date: 2026-11-05. This pass takes the project from "a pile of decompiled
functions" toward the shape of the decomp projects that inspired it:
**named dependencies, an identified input seam, and the corpus split
into per-module files** under `decompiled/by-module/`.

## Imports: 134 of 195 named

Method: a PS3 FNID is `SHA1(name + suffix)` — first 4 digest bytes,
byte-reversed — with the fixed suffix documented on the PS3 Dev Wiki's
NID page (its worked example, `FNID('_sys_sprintf') = 0xA1F9EAFE`, is
asserted in the matcher before any results are trusted).
`scripts/match_fnids.py` hashes the public SDK API surface for each of
the 20 stub libraries and matches it against the game's hashes.
Full results: `analysis/imports.tsv`.

- 100% named: cellAudio (5), cellGame (4), cellPngDec (7), cellRec (5),
  cellSysmodule (2), sceNpTrophy (10), sys_fs (7), cellVideoUpload (1),
  sys_net (3).
- Partial: cellGcmSys 18/36, cellSpurs 11/22, cellSysutil 10/20,
  sysPrxForUser 9/15, cellResc 15/17, sceNp 17/22, cellRtc 2/3,
  sceNpCommerce2 2/3, cellNetCtl 2/4, sys_io 4/6.
- Unnamed: cellVideoExportUtility (0/3) and 61 scattered hashes — they
  stay in the TSV as hashes for future dictionary rounds.

The names are applied in the Ghidra project (`scripts/NameImports.java`):
the import-table slots now carry SDK names, corpus v2 was re-exported
(`analysis/v2/`), and `decompiled/decompiled.c` is now the v2 text.

## The input question is settled: pad only, no built-in keyboard/mouse

All six `sys_io` imports are controller functions. Named:
`cellPadInit`, `cellPadEnd`, `cellPadGetData`, `cellPadGetInfo2`
(slots `0x1F2B00`–`0x1F2B10`); two slots (`0x578E3C98`, `0xF65544EE`)
are still unnamed, but every keyboard/mouse candidate name from both
the official SDK (`cellKb*`, `cellMouse*`) and PSL1GHT (`ioKb*`,
`ioMouse*`) families was hashed and **none matched** — nor did any
non-pad function. So the original never had keyboard/mouse support to
re-enable; the native port's keyboard/mouse layer (milestone 1) is a
port-side addition, as suspected might be the case.

The callers give us the game's input seam (`analysis/v2/import_callers.tsv`,
resolved thunk → caller):

- **`FUN_000dae30`** (3,548 B) calls `cellPadInit` — the input init path.
- **`FUN_00110738`** (2,880 B) calls `cellPadGetData` — the per-frame
  pad sampling function. When game code is ported, this is the function
  whose native counterpart consumes the host's `InputFrame`.

## Function → string references, recovered outside Ghidra

Ghidra's database holds no references for this binary's string traffic:
code loads string pointers from TOC slots (`lwz rX, disp(r2)`, TOC base
`0x1FC7C0` from the entry OPD), and Ghidra never established the r2
context, so neither direct nor slot references exist. Two recovery
steps, both scripted and reproducible:

1. `scripts/find_string_slots.py` scans the writable segment for words
   equal to a defined string's address → **1,158 string-pointer slots**
   (`analysis/v2/string_slots_map.tsv`).
2. `scripts/toc_string_xrefs.py` disassembles the code sections with
   Capstone (PPC64 BE, skip-data on) and resolves `lwz/ld/addi …,disp(r2)`
   plus `lis`+`addi/ori` address materialization against the slot map →
   **335 function→string references across 79 functions**
   (`analysis/v2/function_strings.tsv`).

Known gap: assert strings (e.g. `arenaGraphics.cpp:424`) sit in
consecutive slot *arrays* that code indexes from a base pointer, so
their owning functions don't show up individually yet — array-base
resolution is a noted next step.

## Module map v1 (`decompiled/by-module/`)

`scripts/split_modules.py` assigns each function by (a) the strings it
references, then (b) one hop of call-graph propagation from those seeds,
labeled "probable" in `manifest.tsv`:

| Module | Functions | Evidence |
|---|---|---|
| engine_multistream | 66 | cellMS name-table strings + neighbors |
| engine_psgl | 21 | PSGL/_js strings + neighbors |
| game_local_text_cpp | 9 | local_text references + neighbors |
| runtime_c_library | 17 | Dinkumware paths + neighbors |
| unassigned | 730 | no string/call-graph evidence yet |

87% unassigned is the honest state of play after two passes — the bulk
of the game (and the statically linked libraries' quieter functions)
references few strings. The decomp projects this is modeled on got
their maps from months of exactly this work: more seeds (import call
sites per subsystem, data-structure fingerprints, vtable shapes), then
human naming. The map's job now is to make that work navigable.

## Also landed

- `analysis/language/` — the extracted string tables. The `.utf` files
  are plain text (entries separated by `****` lines, loaded by index):
  **265 English strings** in language1, menus included (Training /
  Campaign / Battle / Settings / Online Scores / Credits / Resume /
  Paused / Restart Game / End Game / Main Menu / Buy Game / Continue…).
  The other 12 files carry 57–70 strings each.
  Extractor: `scripts/extract_language.py`.

## Next steps

1. **Boot walk**: entry OPD → `0x10230` → `main`; rename the startup
   chain and the `gCometEnv` init path in Ghidra.
2. **More clustering seeds**: per-library import call sites are already
   in `import_callers.tsv` — functions calling cellGcmSys/cellResc are
   renderer-adjacent, cellFs users are data loading, etc.
3. **Assert-array resolution** to pin the game TUs (arenaGraphics,
   ObjectGroup, sound/engine) precisely.
4. **Menus in the native host** from the extracted string table —
   title → main menu → mode screens, driven by the unified input.
5. Start renaming game functions in Ghidra from the seams outward
   (input first: `FUN_00110738` and `FUN_000dae30`).
