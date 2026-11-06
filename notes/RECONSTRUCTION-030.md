# Reconstruction notes 030 — the "sprite atlas" is the
game's glyph engine; two new boundary-free scan tools

Date: 2026-11-06. Follow-up to notes 028's open loader
hunt.

## New tools (scripts/)

- scan_toc_disp.py: sweeps for instructions using r2 with
  a given displacement. Single-instruction slot accesses
  (disp within +-32768) need no addis/addi pair, which is
  why scan_raw_xrefs.py saw nothing for these slots.
- scan_abs_addr.py: sweeps for addis+addi/ori pairs
  forming an absolute address. For 0x9FAB50 / 0x5D1FD4
  it found NOTHING — those addresses reach code only
  through their initialized TOC pointer slots.

## What the scans found

- The atlas-object slot (-0x5C24) is loaded by the whole
  GUI/text cluster: the container draw (FUN_00134e28),
  gui_setWidgetSprite, the hud_text* renderers, and
  FUN_00146b98 (the 23 KB unidentified function — now
  known to be a heavy GUI-system user, 11 loads).
- The table slot (-0x5640) users cluster at
  0x1597B0-0x15D000 plus an uncarved gap run at
  0x15C6F0-0x15C948 stepping the table in a regular
  pattern (future carve target).
- FUN_00159de8 = gui_measureText (renamed): walks a byte
  string, resolves each char's record through the same
  ushort map, sums the record advance (+0x18). Record
  layout so far: +0x08 w, +0x0C h, +0x18 advance,
  +0x1C page (ushort).
- FUN_0015adf8 = gui_renderText (renamed): references
  "diffuseMap", "modelViewProj" and the path prefix
  "images/" — the glyph engine renders textured quads
  with the GUI shader and a texture from data/images/.

## Status of the loader hunt

The atlas is the game's central glyph system (all GUI
text + icons, ids into the thousands). Its table and
object are runtime-loaded into BSS (0x9FAB50 /
0x5D1FD4) through initialized TOC pointers; no code
materializes the addresses directly and no filename
string names the source. Remaining candidates: a file
under data/images/ loaded by an initializer that gets
the buffer from the same slot loads (indistinguishable
from readers by these scans), or an embedded blob.
Next decisive step: build a caller map for the whole
0x159xxx-0x15Dxxx cluster and find the non-cluster
caller that runs at boot/GUI init.

## Addendum (same day): the 0x15C128-0x15CAC8 "gap run"
is gui_renderTexts tail

## Addendum (same day): the 0x15C128-0x15CAC8 "gap run"
is gui_renderText's tail

No prologues and no OPD/vtable entries point into
0x15C128-0x15CAC8. Extending gui_renderText's body over
it (4,912 -> 7,376 B) decompiles cleanly (625 lines, no
halt_baddata) and the extension references the glyph
table 11 times — the table-stepping run was the
renderer's own glyph loop all along, cut off by the
same computed-branch carve limit as f3db0/105d80.
Corpus still 2,647 functions, 0 failures.
