# Reconstruction notes 015 — AnyPS3 cross-check

Date: 2026-11-06. Resource from Jerry: github.com/maceeek/AnyPS3
— an experimental static recompiler (GPL-2.0) that translates
PS3 PPU/SPU code ahead of time to C++ with its own LV2 kernel
and a Vulkan RSX backend, producing native Linux executables.
Its only proven target so far is Ratchet & Clank: A Crack in
Time. Cloned at ~/tools/anyps3-ref on the desktop.

## Cross-validation of our corpus (its `inspect`/`analyze`)

Run against our decrypted EBOOT.ELF, it independently reports:

- entry OPD code 0x10230, TOC 0x1FC7C0 — matches notes 003;
- **20 import libraries / 195 imported functions** — matches
  the .lib.stub decode of notes 002 exactly;
- **2 embedded SPU images** — matches the EU build's count;
- 915 OPD descriptors / 851 unique code entries — corroborates
  our 868-function Ghidra corpus.

Its analyzer leaves import names unresolved (NIDs only), so
our FNID naming (170/195, notes 008) is ahead of it there.

## Can it recompile Comet Crash today? No

Its recompiler survey supports 68 of 851 entries; the top
blocker is the ordinary `mflr` prologue instruction (390
entries), with more link-register and shift variants behind
it. The translator is early, and the LV2 surface this game
needs (cellRec, trophy, commerce, Spurs job overlays) goes
well past its one proven target. Verdict: not a route to a
running Comet Crash now. It stays in the toolbox as an
independent validator, and its knowledge base
(docs/knowledge: CELL/PPU/RSX/SPU) is reference material for
the rendering work.
