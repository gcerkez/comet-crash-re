# RECONSTRUCTION-036 — Laser colour bug: Battle-mode investigation and the road to a live repro

Date: 2026-11-07. Status: bug NOT yet reproduced headless; the tooling
gap that blocked multiplayer driving is closed, Battle mode's map
loading is mapped, and a concrete gdb-based repro plan is recorded.

## The bug (Jerry's report, 2026-11-07)

Kind-23 Laser structures render with purple/green rainbow shades that
are not in the original game (his gameplay screenshot; turrets,
pulsars, bases and rocks in the same frame render correctly).

Offline checks already eliminated the texture files: structLaser_DC /
_SCSE.dds are uncompressed 32-bit (masks 0xff/0xff00/0xff0000/
0xff000000, single mip), the same format family as the correctly
rendering structTurret textures, and the .mtl follows the standard
pattern (map_Kd = _DC, map_Ks = _SCSE). Not a texture-file or
format-table issue. Suspect: the guest fragment program for the laser
models (a different shader from the turret's) getting a wrong
texture-unit binding or swizzle in the fork's guest-program path.

## Tooling: PAD_FILE2 (fork commit 7adc9739)

Every scripted input mechanism in the toolkit drove port 0 only, and
YDKJ_INJECT_PAD's 15/180-poll pulse races the game's per-frame pad
sampling (it never joined Player 2). Two changes to
libs/input/cellPad.c:

1. PAD_FILE2=<path>: the PAD_FILE contract for port 1 (same
   "0xMASK [count]" encoding, consumed on admission).
2. While PAD_FILE2 is set, pad_poll_backend reports port 1 connected
   (assigned before pad_track_assign, so cellPadInit's baseline
   includes it). This half is essential: the title discovers pads via
   cellPadGetInfo and never polls a port it believes is empty —
   without it, GetData(1) is never called and the file is never read.

Verified end-to-end: main menu → Battle → setup → map select → join
screen, Player 2 joined (START) and readied (CROSS) from the file,
match started. Launcher: cc_launch2p.sh (staging) sets both files.
Logged as RUNTIME_PATCHES entry 10.

## Battle mode: what loads what (all verified in the running game)

- Map indexing CONFIRMED: terrain file = 100 + world*5 + area.
  (Kreios, 1) opens level101.map; flipping the world selector opened
  level106.map, then level111.map (preview loads at selection time).
- At match start the terrain file opens a second time — and it is the
  ONLY map file loaded. The 15x/16x/17x "structure" files are NOT
  opened by Battle: the (world 2, area 1) match never opened
  level162.map despite the +51 pairing hypothesis (101↔152 at Kreios).
- level152.map opens once at boot (the known startup probe) and once
  more around map-select entry. What consumes 152, and which feature
  the pre-built-army files 160–176 belong to, is still OPEN.
- Battle arenas start with NO pre-placed structures: terrain rocks
  only, no visible ships in the overview, a dashed square marker at
  the arena's centre feature, and flying-rock events running. The
  ship/base placement mechanics were not cracked headless in this
  session (dpad taps did not move the marker; the R1 palette did not
  open). Battle therefore cannot supply a pre-placed laser repro.

## Palette mechanics (verified in Training)

Training is a single fixed tutorial mission on the Kreios-1 campaign
map (no mission list); its palette is locked to Turret. Mechanics:
hold R1 → a vertical structure palette appears beside the ship with
the selected structure's name and cost; dpad changes selection while
R1 is held; release R1 to pick; dpad/stick moves the ship (and ghost);
CROSS builds. PAD_FILE combined masks (e.g. 0x0840 = R1+DOWN) drive
all of it.

## Next step: gdb-assisted campaign repro

Campaign areas past 1 are locked on a fresh profile, but the embedded
levels are all in the ELF and level_load_file takes the index as an
argument:

1. Fresh launch; gdb attach; breakpoint on the lifted level_load_file;
   drive Campaign → Area 1 and confirm the index argument (expect 1).
2. Relaunch; at the breakpoint, rewrite the argument to 2 and
   continue. Embedded level 2 carries 3 pre-placed lasers beside
   8 turrets (kinds {21: 8, 23: 3, 29: 1, 10: 3, 11: 1, 9: 59}) —
   screenshot the overview and compare laser vs turret colours in one
   frame. Levels 3 (3 lasers), 5 (3) and 15 (6) are fallbacks.

With a live repro, compare the laser draw's bound textures/program
against the turret's (guest-program logs / VK texture dumps) to pin
the binding fault.

## Addendum (2026-11-07, later): gdb observation results

One observation run was completed before GPU testing was paused at
Jerry's request (the recomp is GPU-intensive on his desktop; relaunch
only with his go-ahead). Breakpoints on the lifted level_parse
(func_000EE4D8, at 0xf694c0) and level_load_file (func_000E9E7C, at
0xf6f9e0) DO set and hit — an earlier truncated log read suggested
they hadn't; the full log shows three hits during a Campaign →
Area 1 drive (ctx gpr offsets: gpr[3] at +24, lr at +1032):

- `PARSE lr=154a10 g3=110ec7d8 g4=1 g5=feff2f0 g6=1` — the descriptor
  path; g3 is a guest pointer into a level descriptor (+0x1A8 per the
  lifted caller), g5 a guest stack buffer. This is the likely
  campaign embedded load.
- `LOADF r3=3204608 r4=0 lr=f7fa8` — level_load_file's r3 is
  0x30E600, the game-state struct base, NOT a level index. The corpus
  signature's "index" reading was wrong: the loader takes the state
  pointer and the selected level lives in a field of that struct.
- `PARSE lr=e9ee4 g3=871ee4 g4=1 g5=fef5750 g6=0` — level_load_file's
  own parse call, i.e. one complete file-based load in the window.

Revised patch plan for the next session: break on level_parse with
lr==0x154a10 during a campaign start; dump the descriptor via vm_base
(descriptor base = g3 − 0x1A8). The lifted caller walks descriptors
of 0x22C bytes (index × 0x22C), so identify the level-1 descriptor's
table and the level-2 entry, then either swap g3 to descriptor[2] +
0x1A8 (checking g4/g5 don't also encode the level) or, safer, find
and rewrite the selected-level field inside the 0x30E600 struct
before the load fires. As before: a successful swap shows level 2's
3 lasers beside its 8 turrets in one frame.
