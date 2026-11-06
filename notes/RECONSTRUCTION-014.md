# Reconstruction notes 014 — the rock economy, corrected; bases; routing

Date: 2026-11-06. Jerry's correction: the milestone-10 "meteors
on a timer paying a flat 20" was invented. The real mechanic is
in the data, and it is a difficulty/reward system: different
rocks pay different amounts, and maps provide different amounts
of rocks. This note replaces that part of notes 013.

## The mechanic, from the game's own text

Training strings (language1.txt) describe it exactly:

- 122: "Bring flying rocks near your Turrets to destroy them
  and collect Thorium."
- 123: "Grab flying rock: Hold [button]" — the ship grabs rocks
  and drags them.
- 125: the Thorium supply has a cap ("If the bar is full, your
  ship can't gather any more").
- 129: "Build a maze out of Turrets to make the Enemy Path
  longer... You can't fully block the Enemy Path."
- 260: missions report a "Thorium rate: %d per minute".

## The data

- **Rock events are secondary records on selectors 20/21/22** —
  a channel the structure keep-filter discards. Selector 20
  carries a cell (bytes 4/5) where the rock appears and a payout
  pair at bytes 10/11 — equal pairs (40,40), (60,60) and ranges
  (10,20), (60,150), (100,200): read as a (min, max) payout.
  Selectors 21/22 carry the payout in the opcode (10..60) and no
  cell. Maps differ widely: level33 is rock-rich, level61 has
  none in its loose file.
- **Construction events name their cell too**: every secondary
  record carries grid (bytes 4/5); the script builds structures
  at those cells — level33 has *no* primary Basic Ops records,
  yet its script builds a factory cluster at (22,5)+. So events
  create structures at cells, not only activate dormant ones.
- **Base markers are primary records**: type 10 with raw[1] =
  1/2/3 are the player base/start cells (three, matching the
  3-player co-op), and the single type-29 record is the enemy
  base. Level61: players around (11,14), enemy at (3,2).
- Type 31 exists as a primary (level33 has one at the cell its
  selector-11/12/14 opcode-31 events keep addressing) — a
  special object, still unidentified.

## Native milestone 11 — implemented

- LevelData decodes rockEvents (selector 20/21/22 with payout
  range and optional cell) and the base markers.
- Rocks spawn on the level's own schedule with their own
  payouts (deterministic pick within the range); no periodic
  spawner, no flat payout, and structure kills no longer pay
  Thorium — rocks are the only source, capped at thoriumMax.
- Grab: holding Alt near a rock pulls it onto the ship for
  dragging (build-kind cycling moved to R1).
- Construction events are cell-addressed: dormant-at-cell →
  any dormant of the kind → fresh construction at the cell.
- Bases anchor the game: the ship starts at the player base;
  ground units route base-to-base with BFS pathfinding
  (rocks and built structures block, dormant sites don't, a
  Gateway passes only its owner's side — its real function),
  and player construction is refused when it would seal the
  enemy's route, per string 129 and the recovered grid
  transaction.

Tests cover the decoding, per-rock payouts, the grab, script
construction at cells, and routed base arrival; ctest 5/5.
