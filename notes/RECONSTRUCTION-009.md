# Reconstruction notes 009 — cross-check against Stylish-Dark's decomp

Date: 2026-11-05.

Jerry pointed at another public decompilation of this game:
[Stylish-Dark/Comet-Crash-1-Decomp](https://github.com/Stylish-Dark/Comet-Crash-1-Decomp)
(a native PC port effort built on ps3recomp lifting). Their
reference build is **NPEB00142 v1.00** (EU PSN); ours is
**NPUB30061** (US PSN) — different executables, so their PPU
addresses do not transfer. Their *data-format* work does, and it
cross-validates ours to the byte.

## What their level-map recovery confirms and adds

Their loader analysis (their `0x000D91B0`, our `level_parse`)
matches ours exactly: the 0x88 header, the size equation, the two
runtime vectors at state +0x88/+0x98, and the derived fields at
+0xA8/+0xAC. On top of that, they resolved semantics we had open:

- **The type-0x0B primary record is the arena extent marker.**
  extent = max(record bytes @2, @3), written to both derived
  fields; when no marker exists the fields default to 24. Observed
  extents: 16 or 20. The arena is extent × extent cells — this is
  the grid the parsed content lives on.
- **Secondary records are a filtered event list.** The loader
  consumes the section *only when an extent marker exists*, and
  keeps a record iff selector (@6) ≤ 10 and (selector ∈ {9,10} ?
  opcode (@7) ≠ 29 : opcode ∈ 20..30, ≠ 29). The float @0 is
  monotonic nondecreasing across every known map — a time/order
  key, i.e. these are the level's scheduled spawn events.

Both rules are now implemented in the native `LevelData` (extent +
`events`), with unit tests, and reproduce their published
per-level table exactly on our data (level 0: extent 16, 1 event
kept; level 13: 20, 0; …). Loose level61: extent 16, 83/83 kept.

## The embedded levels are extracted

Their project extracts the EU build's 29 embedded level blobs. Our
build embeds more: the table (reached via the TOC slot at
TOC−0x6B50, table VA 0x1F1EA0) holds **56 entries (levels 0–55)**,
all validating against the size equation; entries 43–49 alias
level 1's blob, leaving **49 unique levels**. The first 29 blobs
are byte-size-identical to the EU table (same counts, same order)
— the level data did not change between regional builds.
Extracted to `~/Downloads/comet_crash_extracted/extracted/levels/`
(game data stays out of git); the index is
`analysis/embedded_levels.tsv`.

## Input seam detail (from their input reversing)

The pad packet `pads_sample` decodes: digital button words at
packet +0x08/+0x0A, the four analogue bytes at +0x0C/+0x0E/+0x10/
+0x12, axes centered on 128 and normalized into the game's input
state. That is the exact shape `frame_input` consumes and the
contract the native InputFrame already mirrors.

## Not transferable

Their function addresses, their root-state offsets
(game_state+0x2D6438 etc.), and their ps3recomp lifting artifacts
belong to a different build. Where their *structure* suggests a
hypothesis for ours (e.g. their single huge arenaGraphics.cpp
function holding the render-target setup lines 1034–1188), it is
recorded as a hypothesis only — our analogous function is still
identified by our own evidence.

## The branches beyond main (checked 2026-11-05, Jerry's prompt)

The default branch is NOT their newest work. Some 60+ branches
exist; a chain of `work/*` branches runs through late October to
**`work/original-player-destruction-20261103` (2026-11-03)**, far
ahead of main (2026-10-23): original player flight/banking, carried
units, attachment states, health display, destruction/reset, a
playable mouse-driven preview, and dozens of recovered subsystem
docs (construction, factory production, cannon/tesla combat,
ground dynamics...). Their own calibrated estimate for a complete
faithful port: **~25% (range 15–35%)**, with original gameplay and
integration dominating the remainder — a useful calibration for
our own status reporting.

Transferable structure from the newest branch:

- **Entities live in banks with an index pool** (FIFO free-index
  ring; a 256-byte template is copied per entity; a bank guards at
  126 full cells). **Cell records are 8 bytes**: entity index
  (u16), a byte from the entity, a marker byte, then **packed
  x/y/z relative position** — position relative to a cell origin
  (coord − 0.5), clamped [0, 2], ×100, truncated to a byte. So
  placement is *grid-cell + packed sub-cell offset*, which fits
  the extent-grid model of the level format.
- Their scenario fixtures confirm unit-kind IDs in the 20s (the
  same numeric range as our secondary-record opcodes 20–28 and
  the language-table unit list) and factory production chains
  (kind → produced unit IDs) — again, structure transfers,
  addresses don't.
