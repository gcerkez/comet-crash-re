# Reconstruction notes 004 — main walked, data formats opened

Date: 2026-11-05. Three results: `main()`'s structure and the frame
chain are named; the `data2.pak` archive format is cracked and its
contents extracted; the `.map` level format is characterized and its
loader chain found.

## main() and the frame chain

`main` (0xDAE30) reads, in call order, as the classic PS3 boot:

1. **Platform boot**: `cellSysutilRegisterCallback`,
   `cellSysmoduleLoadModule`, `sys_net_initialize_network_ex`,
   `cellGameBootCheck` / `cellGameGetParamString` /
   `cellGameContentPermit` (content checks),
   `cellSysutilGetSystemParamInt`, `cellNetCtlInit`,
   `cellSysutilEnableBgmPlayback`, `cellSpursInitializeWithAttribute`,
   `sceNpInit`, `cellPadInit`.
2. **Game run**: `cellSysutilCheckCallback`, then `game_run`.
3. **Shutdown** (reverse order): `cellSpursFinalize`, `sceNpTerm`,
   `cellNetCtlTerm`, `cellPadEnd`, `sys_net_finalize_network`.

The frame chain, each link the (single) caller of the next:

```
main -> game_run (0xD8894) -> game_loop (0xE36E0, ~13 KB decompiled)
     -> frame_input (0xE2D78) -> pads_update -> pads_sample
```

`game_loop` is the large per-frame driver; `frame_input` is the frame
step that polls the pads. All four names are applied in Ghidra
(`scripts/renames.csv`). The caller of `game_loop`'s other helpers and
the loop's render call (cellGcmSys/cellResc side) are the next walk.

## data2.pak — format cracked, contents extracted

`file` calls this archive "TTComp", which sent the first look toward
compression. That label is a **false positive**: the payload is stored
raw (every member starts with an MPEG frame sync). The format:

```
u32 total_size (== file size), u32 file_count
file_count x { u32 name_offset, u32 data_offset, u32 data_size }
name table (NUL-terminated names, right after the record table)
member bytes at data_offset; members are 0x80-aligned, so the gap
between one member's end and the next offset is zero padding
```

Extractor: `scripts/extract_pak.py`. `data2.pak` holds **16 MP3 sound
effects** (160 kbps, 44.1 kHz, mono; whooshes, laser, build sounds,
UI choose) — extracted under
`~/Downloads/comet_crash_extracted/extracted/data2/` on the desktop
(index in `analysis/pak_index.tsv`). These are MultiStream's SFX bank,
and the first real assets the native port can use once audio lands.

## .map levels — characterized, loader chain found

Twelve loose level files sit beside the pak. Structure so far:

- **Header**: eight big-endian u32 fields. Later fields carry
  round numbers up to 1200 (world-extent scale) and small counts;
  exact semantics await the parser code.
- **Body**: big-endian float data throughout. Large sections are
  **unit-magnitude 2D direction vectors** (e.g. (0.23, 0.973),
  |v| = 1.0), sparse among zeros — flow-field-like data, exactly what
  a tower-defense pathing field looks like. Other values run to ~800
  (position-scale data). So a map is several float sections, not one
  grid; section boundaries come from the header counts.

Loader chain (found by disassembly — both format strings,
`"%slevel%u.map"` and `"%slevel%d.map"`, are referenced through TOC
slots in code Ghidra's analysis had missed):

- `level_load_file` (0xE9E7C, created by `scripts/DiscoverPaths.java`)
  builds the path with a snprintf-like helper and loads the file via
  the routine at 0xEE4D8. It indexes a per-level table (at runtime
  address 0x551EBC) by level number.
- A second path builder lives at 0x154828 inside a ~33 KB stretch of
  code Ghidra never discovered (0x14F35C–0x157590) — likely a whole
  module (editor or object system); mapping it is future work.
- Callers of `level_load_file` call it indirectly (no direct `bl`
  references), so the buffer's consumer — the real parser — is one
  more hop away.

The native host already renders what is verified: with
`COMET_MAP=<file>` set, the arena draws the map's direction vectors
(see `native/src/level_data.*`, unit-tested).

## Process note

`toc_string_xrefs.py` silently drops string references whose
instruction sits in a function Ghidra hasn't discovered (it attributes
refs by containing function). Both `.map` format strings were missed
that way. Any future "no function references X" claim from that tool
needs a raw-disassembly cross-check, as done here.
