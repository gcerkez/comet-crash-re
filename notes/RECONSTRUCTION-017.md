# Reconstruction notes 017 — we were working from a patch package; their preview, run

Date: 2026-11-06. Two corrections to our working picture, both
from following Jerry's push for fidelity to the original.

## Our game data was a 15 MB patch, not the game

The NPUB30061 package on the desktop
(UP2009-...-A0110-V0100-PE.pkg, 15 MB) is the v1.10 update
package. It carries only the files the patch touched — which
is why just 2 of the ~44 OBJ models the code names were
present, and why several "mysteries" (missing structure
meshes, a 64×64-looking ground texture) were artifacts of
partial data.

The full base package (190,191,376 bytes, URL in Jerry's own
PS3_GAMES.tsv) is now downloaded and extracted to
~/Downloads/comet_crash_full/. It contains everything the
code's model registry names:

- 44 OBJ models: the four comet terrains (gaseous/ice/lava/
  rock, sculpted meshes ~36 units wide), playerShip (+Donut),
  articulated weapons (turret stand+barrel+clasp, laser
  stand+barrel, missile stand+pivot+barrels), barracks
  basic/adv, base shell/light/platform, tesla, gateway, mine,
  bullet, sun, border, black_ground; units (flyer, beetle,
  agentAdv Carrier/Laser/Medic, scout02, fighter02);
  resourceGeode (the Thorium rock) and rockA.
- cometGaseous_DC.dds is 2048×2048 — the whole painted
  surface, not a tile.
- data1.pak (a second archive; data2.pak was audio) and 29
  loose level files (61+, 101+...).

Our EBOOT corpus stays the 1.10 patch binary (the newer
code); formats agree across both.

## Stylish-Dark's preview branch, built and run here

At Jerry's request, work/original-player-destruction-20261103
(tip 01faff0, 2026-11-03) of Comet-Crash-1-Decomp was checked
out at ~/tools/cc-decomp-ref (local branch jerry-preview).
Facts established by doing it:

- The runnable is decomp/'s `comet_native` (SDL2/OpenGL
  viewer behind -DCOMET_NATIVE_VIEWER=ON; SDL2-devel +
  mesa-libGL-devel + Xvfb installed in the iloader box). The
  port/ tree is their older Windows-only recomp runtime —
  not the preview.
- It runs on OUR US data: --validate-assets loads 37 model
  entries; the arena loads 40 entries / 11 textures.
- Headless runs work under xvfb with --frames N
  --screenshot out.ppm. Two captures taken: the native
  skirmish bootstrap, and level 0 (our extracted embedded
  map) with its construction config — the level's rock
  records render as placed 3D rock meshes on the sculpted
  comet terrain, inside their debug construction sandbox
  (build toolbar, free camera, status overlay).
- Their own README on this branch: "A preview runs; the
  original player loop and first original mission are not
  yet accepted", full-port estimate ~25%. Their simulation
  depth (weapons, ground steering/dynamics, factory control
  as recovered code) is well ahead of ours; their
  presentation (washed lighting, sandbox UI) is behind the
  original, like everyone's at this stage.

## Consequence for this project

The port now re-points at the full asset tree, and the
presentation work follows the original's own structure:
named models composed as the code composes them (comet mesh
as the field, articulated structure parts), with the render
chain read from the corpus (game_loop → FUN_00078b3c →
frame_present) rather than approximated. Their repo has no
LICENSE file — reference only, as before.
