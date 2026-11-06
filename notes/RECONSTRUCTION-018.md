# Reconstruction notes 018 — asset registry located; full-asset rewire

Date: 2026-11-06. Milestone 13: the native scene now composes
the base game's real models, and the corpus hunt for the
original's asset system got halfway.

## The asset registry is a static struct, not a table

The model/font/shader path strings (0x17CF00-0x17D600) are
pointed to from a dense field block at 0x1F615C-0x1F6400.
Around it (0x1F60C0 up) sits one big initialized static
struct: sequential string-pointer fields (spaced like the
strings, 0x10 apart in string space), then mixed scalars
(float 1.2, float 255.0, handles) and then the asset paths.
72 pointer slots in the window resolve to strings: particle
textures, the wmd_font family, shaders.bin, and every model.

Three Capstone scans (scripts/find_table_users{,2,3}.py)
prove what the access pattern is NOT: no function loads these
slots TOC-relatively (the proven pattern for ordinary
strings, re-verified against level_load_file's "%slevel%u.map"
slot at 0x1F5B30), no stored pointer anywhere aims into the
region, and no TOC slot holds its base. The struct is reached
through a base pointer formed some other way (absolute
addressing in an init path, or via another global). Finding
its consumers is the next Ghidra-session job: it gates reading
the original's model composition and world scale from code
instead of estimating them.

Also corrected: FUN_00078b3c is only a flip trigger (it calls
frame_present when a framebuffer global is set). The scene
rendering lives in its callers (FUN_000d8278 / FUN_000ff6d0)
and below; the 0x78Bxx neighborhood is a texture/framebuffer
resource layer.

## data1.pak = music + the full sound library

data1.pak (base package) is the same raw archive format as
data2.pak: 45 members extracted to
~/Downloads/comet_crash_extracted/extracted/data1/ —
soundtrack MP3s (1_03Consciousness, 2_01DarkCore,
2_02Subversion, 2_05MosaicFrac, ...) plus the complete SFX
set (turret_shot, explosions, money_pickup, out_of_money,
mine_launch, ...). data2.pak's 16 files were the patch's
subset. Wiring music + the full SFX set into the host is a
follow-up slice.

## Native scene rewire (milestone 13)

- SceneAssets loads the registry by name from the data root
  (derived from COMET_MODEL): four comet terrains (selected
  per level by the header's world index), turret stand+barrel,
  laser stand+barrel, missile stand+barrels, barracks
  basic/adv, base shell, tesla, gateway, beacon lamp,
  platBasic, geode, rockA, beetle, flyer, scout, agent
  Medic/Carrier/Laser — each with its DDS (the .mtl .tif
  names fall back to same-stem .dds; absolute developer paths
  in .mtl files — /home/jbates/pelfast/games/wmd/... — are
  stripped to basenames; the project's internal codename was
  "wmd").
- Structures compose from parts on platforms; dormant sites
  show just the platform. Terrain rocks (rockA) are placed
  with their records' quaternions (addModel takes a full
  quaternion now; arena_sim carries it from the record).
  Flying Thorium rocks are geode meshes. The ship is
  playerShip.obj (scale from its true radius 0.521).
- World scale estimate: 1 cell = 1.5 world units (comet span
  36 over a 24-cell arena). Verified loading headless:
  `scene assets: ... terrain=1 turret=1 geode=1`, ctest 6/6.
- COMET_SCREENSHOT=<file.bmp> (with COMET_NATIVE_FRAMES)
  captures the final frame — used to eyeball this milestone
  headlessly (level33 renders its real comet, rock fields
  and platform rows).

## Open after this pass

- World fit: on level33 the player base row sits at the
  comet sculpt's falling edge in our fit; the original's
  grid-to-terrain mapping (from the registry struct's
  consumers) decides the true scale/offset.
- Registry-struct consumers; render traversal read.
- Music + full SFX wiring from data1.pak.
