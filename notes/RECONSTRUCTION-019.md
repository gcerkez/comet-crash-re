# Reconstruction notes 019 — code is the definition of truth;
the hub is where the answers live

Date: 2026-11-06. Jerry's standing rule, stated plainly after
milestone 14: **the decompiled code is the definition of
truth; screenshots of the shipped game only measure how far
off we are — they are not targets to tune toward.** Anything
in the port that is not yet from the code must be labelled as
an estimate, never presented as matching the original.

## Milestone 14 — what is data/code, and what is not

From the data (solid):
- Menu-driven level loading. The shell holds a data root
  (COMET_DATA; COMET_LEVELS for the embedded set; COMET_MAP
  remains only as a dev override). StartTraining loads
  level 0, StartMission loads the selected mission's level
  (actionParam + 1), StartBattle loads the first probed
  battle map. The game boots to its title screen and the
  player picks — as the original does.
- The game's own UI font everywhere: BMFont binary
  (wmd_font_readable, "Proxima Nova Rg", 95 glyphs,
  lineHeight 33) parsed directly, glyphs from its DDS page.
  That page is a 16-bit luminance+alpha DDS — the decoder
  now accepts 16-bit uncompressed (mask-driven), with a
  regression test in dds_texture_test.

NOT from the code (estimates, labelled as such):
- The camera (moved closer, less tilt at Jerry's direction)
  — constants are still mine.
- The HUD panel geometry/labels (left panels, clock, score
  line) — arranged by eye from the reference screenshot.
  Only the font, the "Thorium" label (string 217), and the
  underlying sim numbers are the game's.

## The xref hunt: four blind methods, one breakthrough

The asset registry (static struct, string fields at
0x1F615C+) has NO consumers findable by: (1) Capstone
function-bounded slot scans, (2) value tracking, (3) stored
pointer scans, (4) Ghidra's own reference manager
(scripts/FindStructUsers.java — 0 refs, even to the
arenaGraphics strings and their slots).

A boundary-free raw sweep of the whole text segment
(scripts/scan_raw_xrefs.py) found them immediately: dozens
of r2-relative references into 0x1F5E00-0x1F5E4C from
addresses in the UNCARVED region after FUN_000f6990 — the
fragmented mission-setup hub (level_load_file's call sites
at 0xF77F4-0xFA30C live there) — walking struct fields
sequentially (0x1F5E00, +4, +8, ...). One more hit inside a
gap after game_loop (0xE9714 -> 0x1F6010). Conclusion: the
struct likely spans ~0x1F5Dxx-0x1F64xx as one object, and
its consumers were invisible solely because Ghidra never
carved the hub into a function, so every function-bounded
method (ours and Ghidra's) skipped that code.

## Next, in order

1. Carve the hub region (and the post-game_loop gap) into
   functions in Ghidra (CreateRegionFunctions.java exists
   for this), re-export the corpus, and read the hub: it
   holds mission setup and the struct's field semantics.
2. From that code: the original's camera/view definition
   and HUD layout — replace the milestone-14 estimates with
   code values as they are recovered.
