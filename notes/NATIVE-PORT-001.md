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

Render-path status (notes/RECONSTRUCTION-008.md): all 36 cellGcmSys
imports are named (via RPCS3's HLE tables), and the frame chain is
traced to the present step — `frame_present` (cellResc convert+flip /
raw GCM flip) called from the render step under `game_loop`. RPCS3's
RSX shader decompiler is the reference for turning the game's
compiled Cg programs (.fpo/.vpo) into GLSL if an effect needs it;
otherwise shaders are replaced by role (font, gui_quad, ...).
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

**Settled by the FNID pass (RECONSTRUCTION-002):** the six slots are
`cellPadInit`, `cellPadEnd`, `cellPadGetData`, `cellPadGetInfo2`, and
two still-unnamed hashes (`0x578E3C98`, `0xF65544EE`). Every
keyboard/mouse candidate name from both SDK families (`cellKb*`,
`cellMouse*`, `ioKb*`, `ioMouse*`) was hashed and none matched — the
original has **no built-in keyboard/mouse support**; it is pad-only,
and keyboard/mouse in this port is a native-side addition. The game's
input seam is identified: `FUN_000dae30` calls `cellPadInit`,
`FUN_00110738` is the per-frame `cellPadGetData` sampler.

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

## Milestone 2 — the game's menus, running natively

- `native/src/text_table.*`: the extracted language1 table as a C array
  (generated by `scripts/gen_text_table.py`), index-compatible with the
  original's `local_text` loader — `text(i)` returns the game's own
  strings.
- `native/src/menu_model.*`: the menu flow as a pure, unit-tested state
  machine over the unified `InputFrame`. Screens: Title → Main Menu
  (Training / Campaign / Battle / Settings / Online Scores / Credits,
  the table's own order) → Campaign area list (the 22 mission briefings,
  strings 150–171) → mission brief → arena; Battle setup (Number of
  Players / Difficulty / Max Hits / Change Map / team rows / Start
  Game); Settings (Music/Effects Volume, Video Record, Display Zoom
  with its help text, Language, Controls list); Scores (shows the
  original's "Not connected to PlayStation(R)Network." — the service is
  gone); Credits (full original credit block, scrollable). In the arena,
  Start opens the pause menu (Resume / Restart Game / Hide Menu / End
  Game) with the original's Yes/No confirmation dialogs.
- Entering a mode lands on the arena placeholder (grid + briefing);
  gameplay rendering is the PSGL-shim milestone. Value ranges the
  strings don't pin down (Max Hits 1–20, volumes 0–10) are host-side
  approximations, marked as such in the native README.

## Milestone 3 — real game data in the host

- `data2.pak` format cracked (notes/RECONSTRUCTION-004.md):
  `scripts/extract_pak.py` extracts all 16 MP3 sound effects — the
  first real assets available to the port (audio milestone will play
  them).
- `native/src/level_data.*`: exploratory `.map` parser — the header
  fields plus the map body's direction-vector section (unit-magnitude
  float pairs, flow-field-like). With `COMET_MAP=<level file>` the
  arena renders those vectors; unit-tested like the rest.

## Milestone 4 — sound

- The extracted SFX bank plays in the host: minimp3 (CC0, vendored)
  decodes the MP3s; `native/src/sfx_player.*` queues one-shots on an
  SDL audio stream (MultiStream's role). Menu Confirm plays the
  bank's own item-choose click; arena taps play its build sound.
  Point `COMET_SFX_DIR` at the extracted bank;
  `COMET_SFX_SELFTEST=1` exercises decode + playback headlessly.

## Milestone 5 — real level parsing

- The `.map` format is cracked (notes/RECONSTRUCTION-006.md) and
  `native/src/level_data.*` implements it exactly: 136-byte header,
  orientation records (type + quaternion + field), object records
  (value + type/subtype bytes), with the header-count size check the
  original's layout implies. The arena renders the parsed records
  from `COMET_MAP`; all twelve shipped maps validate.

## Milestone 6 — 3D scene: the game's own geometry, rendered natively

- `native/src/obj_model.*`: Wavefront OBJ loader (the shipped models
  are v/vt/vn triangle meshes; fighter02 = 16 verts / 28 tris),
  unit-tested.
- The arena now renders a projected 3D scene (CPU projection +
  SDL_RenderGeometry — the stand-in where a desktop-GL backend will
  sit, per the PSGL mapping in notes/RECONSTRUCTION-007.md): ground
  grid sized from the level header, the loaded model lambert-shaded
  and painter-sorted under an orbiting camera. `COMET_MODEL=<obj>`
  picks the model; `COMET_START_ARENA=1` boots straight into the
  arena for headless smoke tests.

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
