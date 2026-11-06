# Reconstruction notes 011 — cross-resource audit; placement cracked; construction lands

Date: 2026-11-05. Jerry's direction: cross-check every resource
already provided and reuse what others have figured out before
inventing anything.

## The audit

- **awesome-rea-games catalog**: no Comet Crash entry exists —
  nothing further to harvest; this project remains the
  catalog-style entry for the game.
- **RPCS3 sources**: already harvested for FNID names (notes 008).
  Remaining value is reference-only (RSX shader decompiler,
  cellGcmSys semantics). Nothing gameplay-side to reuse.
- **PS3 OpenGraphics Toolkit**: rsxdeasm stays queued for the
  shader files; nothing gameplay-side.
- **Stylish-Dark/Comet-Crash-1-Decomp (all branches)**: the rich
  vein. Their newest branch (2026-11-03) recovers, with documents
  and native code: construction (funded requests, grid occupancy
  transactions, timed completion, eight structure constructors for
  opcodes 20–26 and 28), entity banks/cell records, ground-unit
  spawning from production orders with RNG jitter, factory
  production, and the level-format semantics adopted in notes 009.

## Placement: cracked from our own data

Their docs still treat primary records as opaque, so placement
came from profiling our maps: record bytes raw[2]/raw[3] span
0..23 and plotting them draws the levels — winding walls, rock
fields, a solid plateau block in level0. **Primary records are
placed at grid cell (raw[2], raw[3])**, multiple records may share
a cell, and the record's quaternion is the placed thing's
orientation. raw[1] (values 1..6) is a small class field (owner or
variant — open). The type-0x0B record's raw[2]/raw[3] double as
the extent pair, per the parser.

## Native milestone 8 — the recovered model, implemented

`arena_sim` now runs the model the audit supports:

- **Placed content**: every primary record becomes a static on
  the grid — rocks (type 9, they block movement and absorb
  shots), structures (kinds 20–28) start *dormant*.
- **The level script activates structures**: a secondary event
  whose time key passes activates one dormant structure of its
  kind; kinds with no dormant match send a comet unit instead.
- **Factories produce**: an active factory (kind 24 in v1) emits
  ground units (kind family 40+) on a timer; they advance on the
  base edge, sidestepping rocks.
- **Towers fire**: active non-factory structures shoot the
  nearest opposing target in range (enemy towers target the
  ship; the player's target units).
- **Player construction**: Confirm at the ship's cell places the
  selected kind (Square/Alt cycles 21..26) for 25 Thorium, with
  an occupancy check and a 2.5 s build timer — the recovered
  funded-request → timed-completion shape. Kills pay Thorium.
  The Build SFX plays on placement.

Verified: new unit tests (placement at (raw[2], raw[3]),
activation by kind, factory production, funded build + timer +
occupancy refusal) and a 900-frame headless run on level61 where
the script activates the level's structures on schedule (20/83
events by t=89).

## Still v1 / open

- Ship damage from enemy fire (towers currently pressure by
  presence; their shots don't yet hurt the ship).
- Ground-unit routing is a sidestep, not the original's routed
  grid movement; spawn jitter and production anchors follow
  their recovered shapes but not their exact RNG.
- raw[1]'s meaning; which kinds are factories beyond 24;
  structure costs/HP from data (theirs are synthetic in tests —
  ours are placeholders of the same kind).
- The fragmented mission-setup hub (notes 010) remains the
  RE path to the original's own consumers.
