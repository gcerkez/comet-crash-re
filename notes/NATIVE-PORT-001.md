# Native port notes 001 — architecture, dependency map, milestone 1

Date: 2026-11-05. Goal (Jerry's direction): break the game down into the
pieces that made up the original, on the road to a **native C/C++ app**
that runs on a PC — mouse/keyboard standing in for a controller, with a
real controller supported when plugged in. This note records the module
breakdown, the full PS3 dependency map recovered from the binary, and the
first built milestone.

## The original, as a set of pieces

Evidence: the Ghidra corpus (RECONSTRUCTION-001), the leaked source-file
names, the game data layout, and — new in this note — the decoded import
table (below).

| Piece | Original evidence | Native home |
|---|---|---|
| Boot / entry | ELF entry is an OPD → code at `0x10230`, TOC `0x1FC7C0` | `native/src/main.cpp` (later: `boot/`) |
| Game environment | global `gCometEnv` (`0x180390`); `mCometObjPath` | reconstructed core, TBD |
| Arena rendering | `arenaGraphics.cpp`; PSGL (OpenGL ES-CM 1.0) statically linked over `cellGcmSys` | SDL + OpenGL (milestone: PSGL-shaped shim) |
| Objects / scene | `ObjectGroup.cpp`; `.obj` model trees under `models/care/...` | asset pipeline + scene module |
| Sound | `sound/engine.cpp`; Sony **MultiStream** (`cellMS*`, ~90 functions) statically linked over `cellAudio` | SDL audio / a MultiStream-shaped mixer |
| Text / i18n | `local_text.cpp`, `fontMapFormat.cpp`; `language1..13.utf` | text module; the .utf format needs decoding |
| Levels / scripts | loose `level*.map`, `scripts/`, `models/`, `images/` + `data2.pak` (TTComp archive) | data loaders |
| Input | **`sys_io`, 6 functions** (see below) | **done — milestone 1** |
| Files | `sys_fs`, 7 functions | `std::filesystem` |
| Platform glue | `cellSysutil` (callbacks/dialogs), `cellGame` (content paths), `cellSysmodule` (module loads), `sysPrxForUser` (threads/time/libc glue) | host glue in `main.cpp` / SDL |
| Online & PSN | `sceNp` (22), `cellNetCtl`, `sys_net`, `sceNpTrophy` (10), `sceNpCommerce2` | stub out; the 2009-era services are gone |
| Video record/upload | `cellRec`, `cellVideoExportUtility`, `cellVideoUpload`, `ytapi-...` key | drop (dead feature) or local recording later |
| SPU jobs | `cellSpurs` (22) | host threads, if the jobs are even needed |
| Scaling / PNG | `cellResc` (17), `cellPngDec` (7) | renderer scaling; SDL_image/stb |

## The import table, decoded

The ELF's `.lib.stub` records (0x2C bytes each, at `0x175C3C`) name every
PRX library the game imports from, and how many functions: **20 libraries,
195 functions**. The function-pointer table the loader fills lives at
`0x1F2818` — that's the table the decompiled code keeps calling through
(`*(uint*)DAT_001f28xx`). Per-library FNID hashes are in the stub records;
naming all 195 (matching hashes against public FNID lists) is the next
analysis pass and will rename much of the corpus's call graph.

Full list: cellAudio 5 · cellSpurs 22 · cellSysmodule 2 · cellGcmSys 36 ·
cellSysutil 20 · cellResc 17 · sys_io 6 · sys_fs 7 · sys_net 3 · sceNp 22 ·
cellRtc 3 · sceNpTrophy 10 · cellNetCtl 4 · cellPngDec 7 ·
cellVideoExportUtility 3 · cellVideoUpload 1 · cellRec 5 · cellGame 4 ·
sceNpCommerce2 3 · sysPrxForUser 15.

Statically linked (not imported): PSGL and the MultiStream audio engine —
both are *inside* the 843-function corpus, which is why the corpus is as
big as it is relative to the game's own logic.

## Input: what the original actually does

There is **no `cellPad` import** — and no `cellKb`/`cellMouse` either.
Controller input enters through the low-level **`sys_io`** library
(6 functions, call table at `0x1F2B00`). Its FNIDs, in stub order:

```
0x1CF98800  0x4D9B75D5  0x578E3C98  0x8B72CDA1  0xA703A51D  0xF65544EE
```

This is the same low-level IO family the open PS3 SDK (PSL1GHT) exposes
as `ioPadInit/ioPadGetData/...` — and that family also contains the
keyboard (`ioKb*`) and mouse (`ioMouse*`) functions. A pad-only game
typically needs 4 of the 6 slots (init/end/getInfo/getData), so built-in
keyboard/mouse support in the original is **plausible but unproven** —
it stays an open question until the hashes are matched to names. Either
way, the native input layer covers all three device classes, so the port
doesn't hinge on the answer; the hashes above are the checklist for the
FNID pass.

## Milestone 1 (built): the native host shell

`native/` in this repo: a C++17 + SDL3 app that opens a window and runs
the frame loop with the **unified input layer**:

- `input_types.h` — one `InputFrame` per player (buttons with the PS3
  layout's names, movement vector, cursor, wheel), plus SDL-free raw
  states per source.
- `input_mapper.*` — merges keyboard + mouse + gamepad into the frame:
  edge detection, stick-vs-digital movement, trigger thresholds, and the
  cursor model (mouse owns it while moving; stick/keys integrate it
  otherwise — the original is a pad-cursor game).
- `sdl_input.*` — SDL3 backend: keyboard polling, mouse, and hot-plug
  gamepads through SDL's standard gamepad mapping.
- `main.cpp` — debug view (grid, cursor, button lamps) to verify input
  by eye; `COMET_NATIVE_FRAMES`/`COMET_NATIVE_LOG` env knobs for smoke
  tests.
- `tests/input_mapper_test.cpp` — headless checks of the merge logic
  (part of the build, run via ctest).

Verified on JerryDesktop: builds clean in the Fedora 44 distrobox
(SDL3-devel 3.4.18, gcc 16), the mapper test passes, and the app runs
headless under SDL's dummy video driver for a 30-frame smoke test.
Player 1 only so far; the model extends to 4P by sampling per pad.

## Roadmap

1. **FNID pass** — name the 195 imports in the Ghidra project; re-export
   the corpus with a named call graph. Settles the sys_io question.
2. **Boot walk** — follow the entry OPD chain at `0x10230` to `main` and
   `gCometEnv` init; carve the first real module (game state) out of the
   corpus into `reconstruction/`.
3. **Data formats** — `data2.pak` (TTComp), `.map` levels, `.obj` models,
   `.utf` text: write extractors/decoders in `tools/`, verify against the
   loose files.
4. **Renderer** — a PSGL-shaped shim over desktop OpenGL so arena code
   can be ported call-for-call, then start on `arenaGraphics`.
5. **Audio** — MultiStream-shaped mixer over SDL audio; MP3 streams first.
6. **Input call sites** — once imports are named, find exactly where the
   game samples pads each frame and wire the native `InputFrame` in at
   the same seam.
