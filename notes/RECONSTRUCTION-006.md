# Reconstruction notes 006 — the .map format, cracked from its parser

Date: 2026-11-05. The `.map` level format is no longer characterized —
it is **read from the game's own parser** and verified byte-exact
against every shipped map.

## The parser: level_parse

The routine both level-path builders call (0xEE4D8, previously an
undiscovered function) is the level loader/parser, now named
**`level_parse`** in Ghidra. Its two call sites are the only ones in
the binary. Signature: `level_parse(levelObject, levelNumber, path,
flag)`.

Two data sources, chosen by level number:

- **Levels < 61**: read from an **embedded table** — a pointer array
  of {offset, size} pairs (8-byte entries, indexed by level number)
  into a loaded data blob; reads are memcpy from that blob.
- **Levels >= 61**: read from the loose **`.map` files** with
  fopen/fread-style helpers.

That is why only levels 61+ exist as files: 61–62, 104–120 are the
expansion/custom range; levels 1–60 ship embedded (their blob is a
future extraction target).

## The format

```
header, 136 bytes (read straight into the level object):
    u32[0]  count56
    u32[1]  count24
    u32[2..33]  further fields (round numbers up to 1200 and small
                counts seen; semantics still open)
count56 x 56-byte "orientation" records:
    u8  type @0 (type 0x0B gets special handling — see below)
    float quaternion @4 (x, y, z, w — unit length; e.g. level61's
          first record is a pure Y-rotation (0, .383, 0, .924))
    u32 field @20 (42 in level61's first record)
    remaining bytes zero in shipped maps
count24 x 24-byte "object" records (read only when flag == 0):
    float value @0
    u8 bytes @4..7 — a type byte @6 (the parser branches on
       9, 10, 20, 21–24) and a subtype byte @7 (branches on
       20–32 except 29) — the parser sorts these records into
       different lists by type
    remaining bytes zero in shipped maps
```

**Verification is exact**: `136 + count56*56 + count24*24` equals the
file size for all twelve loose maps, including `level156.map`
(136 bytes = header only, both counts zero — the empty/template
map). The type-0x0B orientation records carry two bytes (@2/@3)
whose maximum the parser stores into the level object (+0xA8/+0xAC,
defaulting to 24 when no such record exists) — likely a size/extent
parameter of that record class.

The "unit direction vectors" reported in notes 004 were the
quaternions' components — the mystery section is orientation data,
not a pathing flow field.

## What FUN_00154888 actually is

Not the parser: it is **level-select screen logic**. It walks a table
of 0x22C-byte level records (index = world*... + slot), formats the
selected level's path, calls `level_parse`, and renders level info —
including a best-time readout it formats from a float via
`"%d:%02d:%02d.%02d"` after decomposing it into h/m/s/cs, looked up
in a records table indexed `[?, level]`. Useful corroboration that
`level_parse` is the real entry point.

## Native side

`native/src/level_data.*` now implements the real format with exact
size validation (a buffer whose length disagrees with its header
counts is rejected), exposing header fields, orientation records,
and object records. The arena renders parsed records; verified
against the real files (level61: 97 + 83; level104: 156 + 0;
level156: 0 + 0). Tests updated to the real layout.

## Still open

- Header fields 2..33 and the orientation records' purpose (spawn
  orientations? turret facings? camera path?) — the consumers of the
  parsed vectors (level object +0x88 for the 56-byte records) are
  the next read.
- The embedded levels 1–60 blob.
- FUN_0014f35c (16.5 KB, 137 KB decompiled, STL `vector` user
  calling mostly small helpers): identity still open.
