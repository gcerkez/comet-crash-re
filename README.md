# Comet Crash — Reconstruction & Recompilation

A reverse-engineering and preservation project for **Comet Crash**
(Pelfast, PSN 2009), the PlayStation 3 tower-offense game: build and
upgrade towers to stop waves of alien craft, stockpile units of your
own, and launch them across the arena to destroy the enemy base
first. 28-stage campaign, offline co-op, and versus play.

This repository contains **only original work**: a hand-written
native port, a static-recompilation project that runs the game's
real code on a PC, the tooling used to analyze the binary, and the
engineering notes produced along the way.

## Legal model — read this first

**Nothing proprietary is in this repository.** No game code (source,
decompiled, or lifted), no game assets, no string tables, no level
data, no encryption keys or license files, and no PlayStation
firmware — and none of it is downloaded by the build.

Everything derived from the game is **generated locally, by you,
from your own copy**:

- You supply a decrypted `EBOOT.ELF` from your own game package.
  The decryption recipe (scetool + your own title license file) is
  documented in [notes/PROVENANCE.md](notes/PROVENANCE.md); the
  license file itself is never committed.
- You supply the extracted game data (base package, the 1.10 patch
  tree, and optionally the Bionic Swarm DLC). The build assembles
  them into a local virtual filesystem.
- The recompilation's audio normally requires a codec plugin from
  the PS3 system firmware. This project instead implements that
  plugin **natively on the host** (a clean-room MP3 decoder seam in
  the runtime fork), so **no Sony firmware is used anywhere** — one
  of the project's goals is a build that is fully self-contained
  and freely redistributable apart from the user's own game dump.

The findings it produced are written up, in our own words, in [notes/](notes/).

## What's here

| Path | What it is |
|---|---|
| [recomp/](recomp/) | The static recompilation — the playable build. The game's own code, lifted to C++ by ps3recomp, running on a clean-room HLE runtime (Vulkan). See its README for the full quick start. |
| [third_party/ps3recomp](third_party/) | Submodule: our fork of [ps3recomp](https://github.com/sp00nznet/ps3recomp) (MIT). Upstream runtime plus this project's patches (catalogued in [recomp/RUNTIME_PATCHES.md](recomp/RUNTIME_PATCHES.md)): the native MP3 codec, the native SPU geometry-job override, fragment-program memoisation, AA-blit fixes, 1080p present sizing, right-stick range boost, pad cadence fix, quit confirmation, pad-file tooling, log discipline. |
| [native/](native/) | A hand-written C++17 + SDL3 port, built from the reconstruction notes (menus, input, level parser, playable arena core, the game's own SFX via minimp3). Independent of the recompilation. |
| [scripts/](scripts/) | Ghidra export/analysis scripts and data extractors (language tables, `.pak` archives) — the tools that produce local analysis artifacts from your dump. |
| [notes/](notes/) | The engineering log: provenance, subsystem findings, format documentation, and the per-fix records (035–052 cover the recompilation bring-up and beyond: rendering, input, DLC, audio, the cheats menu, and the performance work). |

## Quick start (the recompilation)

Prerequisites: a Linux x86_64 machine with `cmake`, `ninja`, a C++20
compiler, Python 3, and the Vulkan + SDL2 development packages
(exact package lists in [recomp/README.md](recomp/README.md)).

```bash
git clone --recursive https://github.com/gcerkez/comet-crash-re.git
cd comet-crash-re/recomp

./setup.sh --eboot /path/to/EBOOT.ELF \
           --gamedata /path/to/base-game-tree \
           --patchdata /path/to/patch-1.10-tree \
           --dlcdata /path/to/bionic-swarm-tree   # optional

./run.sh /path/to/EBOOT.ELF
```

`setup.py` validates the ELF (SHA-256
`a18d8e4a4a2620fd58f7c09e9639f3de91b3197cab3877c554e369dee430a00c`,
2,817,600 bytes, title NPUB30061 v1.10), lifts the PPU and SPU
code, and assembles the VFS overlay in the order base → patch →
DLC. `setup.sh` then configures and builds
`build/CometCrashRecomp`. The game boots to its real menus and
plays: campaign, battle, the Bionic Swarm expansion toggle, sound,
and 1080p output included.

The native port builds separately:

```bash
cd native
python3 ../scripts/gen_text_table.py   # generates src/language1.inc from your extracted language table
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build
```

## Status

- **Recompilation: playable.** Real menus, campaign and battle
  modes, construction, unit auto-send, upgrades, the expansion
  content gate, music and SFX (host codec), 1080p, and a built-in
  Cheats menu (Options and pause; per-player in Battle) that the
  shipped game never had. Performance is an ongoing focus: the
  per-frame geometry job runs as a verified native override by
  default. PlayStation Network features are intentionally stubbed
  (the original's "Not connected" path).
- **Native port: partial.** Menus, input (keyboard/mouse +
  hot-plug gamepads), level parsing, textured 3D arena scene, and
  a playable gameplay core; some per-weapon rules and the exact
  HUD glyph layout remain unported. The recompilation is the
  high-fidelity route.
- Known open items are tracked in the notes and in
  [recomp/README.md](recomp/README.md#known-issues) (audio timbre
  on a few sounds, scene corruption at cheat-fed unit counts far
  beyond the game's supported totals, a first-upgrade color
  glitch, wireless-DS4 stick range at the default boost, a rare
  ship-render blip).

## The target

| | |
|---|---|
| Title | Comet Crash |
| Title ID | NPUB30061 (US PSN), version 1.10 |
| Content ID | UP2009-NPUB30061_00-PELFASTCC0000020 |
| Binary | `EBOOT.BIN` (NPDRM SELF) → decrypted `EBOOT.ELF` |
| Format | ELF64, big-endian, PowerPC64 (Cell PPU / LV2), stripped |
| Decrypted size | 2,817,600 bytes |
| Entry point | OPD at 0x1F2B38 (code 0x10230, TOC 0x1FC7C0) |

## Repository hygiene

Game-derived files are blocked by `.gitignore` (`*.ELF`, `*.pkg`,
`*.rap`, `*.edat`, extracted trees, generated tables, lift output,
VFS, build output). If you fork this: keep it that way. The rule
for contributions is the legal model above — custom code, patches,
and documentation only.
