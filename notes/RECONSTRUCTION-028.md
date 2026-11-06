# Reconstruction notes 028 — sprite resolution chain
decoded; composer trace reaches the shuffle network

Date: 2026-11-06. Follow-ups to notes 026/027.

## The GUI sprite chain (hud_drawFrame's widgets)

- gui_setWidgetSprite (FUN_00135170, renamed): fills a
  widget struct (defaults 1.0 from the 0x1F6Bxx block),
  stores the sprite id as a SHORT at +0x3C, then calls
  the resolver with the atlas object (runtime 0x5D1FD4).
- gui_resolveSpriteSize (FUN_00159d48, renamed), the
  resolver's math, read directly:
    rec = table_base + ushort_map[id] * 0x20
      (ushort map lives in the atlas object at +4 + id*2;
       table base is a runtime pointer, BSS 0x9FAB50;
       records are 32 bytes)
    page_off = rec.ushort@0x1C * 0x5DC2C + 0x7DC40
    widget.w = rec.float@+8  * atlas.float[page_off]   * display_scale
    widget.h = rec.float@+12 * atlas.float[page_off+4] * display_scale
  So sprite ids (0x6BF=1727 frame, 0x6F8=1784 solo
  gauge, 0x6EA-0x6ED toolbar) index a ushort map into
  32-byte sprite records with per-page scale data.
- ELIMINATED: wmd_font_icons.fnt is NOT the source —
  parsed it: BMFont v3, 73 glyphs, ids 12-255, page
  1024x1024. None of the HUD ids are in it. (It is a
  third font set alongside readable/b; unexamined
  beyond this.)
- OPEN: which file the atlas is loaded from. All three
  pointers (atlas obj, table, display scale) are BSS —
  runtime-loaded. No filename string matches, data1/
  data2.pak are all-audio, and an r2-relative scan for
  the table/atlas TOC slots found no loader (access
  likely via another base register). Next angle: a
  boundary-free scan for the addis/addi pair forming
  the buffer address 0x9FAB50 itself.

## camera_composeMatrices raw trace (tilt axis)

Capstone trace of 0x102D40-0x103118 confirms at raw
level: r29 = camera struct base (ctx + 0x2C3950, formed
at 0x102E2C); matrix bases formed as r31-0x2060
(= ctx+0x54DFA0, projection) and r31-0x2020
(= ctx+0x54DFE0, modelview). The composer passes the
struct by pointer to helpers and runs the field data
through a VMX permute/select network — the final
field-to-element assignment is not legible without a
symbolic/dynamic trace. The tilt-axis question stays
documented-open (notes 026/027); the port's 50-degree
elevation reading stands until then.
