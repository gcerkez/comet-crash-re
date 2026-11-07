# RECONSTRUCTION-037 — Bionic Swarm DLC: code present, assets absent

Question (Jerry, 2026-11-07): the Bionic Swarm expansion he bought on
PSN does not enable in the recomp (no PSN to verify against). Is it
already embedded in the project and merely switched off, or a separate
file set?

## Verdict

Both halves, split cleanly:

- **The expansion's CODE is already in the 1.10 EBOOT** (the binary
  the corpus and the recomp both run). String evidence from the
  decrypted ELF itself:
  - Research techs: `RESEARCH GENOME1`, `RESEARCH GENOME2` (the unit
    transformation techs), `RESEARCH NUKE`, `RESEARCH ANTINUKE`,
    `RESEARCH KAMIKAZE` (plus `RESEARCH unused2/3/4`).
  - Expansion asset paths in the registry:
    `models/expansion/harvester.obj`, `harvester_ring.obj`,
    `research.obj`, `defence.obj`, `silo.obj`, `missile_02.obj`,
    `antinuke.obj`, `kamikaze.obj`.
  - Swarm units: `models/swarm1_head.obj` / `swarm1_body.obj` /
    `swarm1_tail.obj` and the swarm2 set (the Scout transformation).
  - Audio: `459967_DragonRoarsVersion1_HS06__7.mp3`,
    `460267_mod_snake_tongue_hiss.mp3` (the Tank transformation).
  - The long-open "Harvester" string (language string 249) resolves
    here: the Harvester is an expansion structure.
- **The expansion's ASSETS are not in Jerry's files.** A filename
  sweep of both extracted trees (full base package and the 1.10 patch
  tree) finds no expansion directory and none of the swarm / dragon /
  kamikaze / harvester files. The models present are the base set.
- The DLC was sold as a separate add-on PKG (US store, 2010-11-16,
  $4.99, 43 MB: 16 campaign levels, 10 battle maps, two new
  environments, on top of the units/structures above). The DLC is
  catalogued as PS3 add-on content (US) for NPUB30061.

## Integration plan (pending Jerry's go-ahead to fetch)

1. Obtain the DLC PKG for the owner's copy, extract it (PSN PKG
   tools already on the desktop).
2. Lay its USRDIR content into the recomp VFS overlay under the
   NPUB30061 tree (same merge pattern as patch-over-full).
3. Boot and look: era-typical add-ons are file-gated (content enables
   when the files exist). If the menus stay dark, find the gate in
   the corpus (the check that guards the research/expansion entries)
   and report exactly what it reads — the standing boundary applies:
   PlayStation Network stays stubbed; this is about local content
   files Jerry owns, not emulating the store.
4. Whatever the outcome, the expansion models/levels then also become
   available to the native hand port's asset set.

## Outcome (2026-11-07, Jerry approved the fetch): ENABLED

- The DLC package (content ID
  UP2009-NPUB30061_00-PELFASTCC00DLC00, 44,630,320 bytes) was
  extracted with extract_ps3_pkg.py: 142 files — the expansion model
  set (incl. Jungle and Scifi environment sets), swarm1/swarm2 model
  families, data3.pak / data4.pak / data5.pak, levels 94, 95,
  121–130, 165–169, and dlc1.edat (272-byte NPDRM EDAT).
  Side-discovery: DLC battle levels 165–169 sit inside the 160–176
  index range whose loose "pre-built army" files in the BASE package
  had no known owner (notes 036) — those slots belong to the DLC.
- Merged USRDIR/data into both VFS mirrors (game/NPUB30061 and
  PS3_GAME; runtime opens resolve under vfs/USRDIR).
- First boot: the game runs its DRM availability check on
  /dev_hdd0/game/NPUB30061/USRDIR/data/dlc1.edat through the sceNp
  HLE, gets "available", and immediately loads data3/4/5.pak and the
  expansion models (harvester.obj et al.) in the asset preload.
- The Campaign setup screen now offers an **Original / Expansion**
  campaign toggle that was not there before the merge. Bionic Swarm
  is enabled and playable in the recomp build; no store/PSN
  emulation was involved — the gate is the local EDAT's presence
  plus the availability answer, and the content is Jerry's own
  purchased DLC from the archive he already uses.
