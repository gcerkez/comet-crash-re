# Reconstruction notes 010 — the level-load hub, and a playable arena

Date: 2026-11-05.

## The level-load hub is a fragmented function

`level_load_file` has exactly one caller region: nine `bl` sites
spanning 0xF77F4–0xFA30C. Ghidra's map fragments that span — the
nearest known function head (FUN_000f6990) is only 256 bytes, so
the true caller is a ~11 KB function the analyzer split. This is
the level/mission setup hub (it loads levels for the game's
modes). Recovering it whole — boundary repair in Ghidra, then a
read of what it does with each loaded level state — is the queued
RE task that leads to the record consumers.

(The naive decompiled-text caller search finds nothing here:
calls in this region render through descriptors, so the caller
list came from a Capstone branch scan, the same technique that
found level_parse's callers.)

## Native milestone 7 — the arena is playable

`native/src/arena_sim.*` is a gameplay core built from the
recovered structure:

- the arena is the level's extent × extent grid;
- the level's filtered secondary records drive a spawn scheduler
  (time key → spawn; opcode → unit kind);
- the player ship flies on the grid from the same InputFrame the
  original's frame_input consumed, and fires on the Attack button;
- units advance on the base edge; leaks cost base hits (limit 10);
  clearing the schedule with an empty field wins the wave.

The shell renders the ship as the game's own OBJ fighter *on the
grid*, units as markers, shots as points, with a TIME/UNITS/KILLS/
BASE HUD; the Laser SFX fires on shots and kills. Entering the
arena (or Restart) resets the sim from the loaded level.

Verified headlessly against real data (level61, `COMET_START_
ARENA=1` demo pilot flying and firing): spawns follow the level's
schedule (20 of 83 events by t=89), kills accumulate (9), leaks
cost base hits (3), clean exit after 900 frames. Unit tests cover
spawn timing, flight, leaks, wave-clear, and combat (ctest 5/5).

**v1 simplifications, stated plainly:** spawn x positions are a
deterministic spread (true placement waits on the record
consumers), unit speeds/HP and the 6× event-time pacing are
placeholders, and there is no building/towers yet — construction
is the next gameplay system, and Stylish-Dark's branch docs on
entity banks/construction are the structural reference for it.
