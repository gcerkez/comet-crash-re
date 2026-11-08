# 049 — Cheats stage 3: per-player Battle cheats + the Player selector row

Date: 2026-11-08 (overnight session, branch `muse/overnight-2026-11-08`)

## Request

Two follow-ons to the stage-1/2 cheats (notes 041):
1. Cheats must work in Battle mode **per player** (up to 4 players),
   settable individually from the cheat menu without overloading the
   view, designed to look native.
2. A ship-health cheat (the player's ship can still be destroyed and
   force a respawn). Status: **not in this note's build** — the ship's
   health field is still unlocated (see "Ship hunt" below).

## Battle structure, verified live (driven 2P session, RAM reads/pokes)

- The game's own language table (string 220) describes Battle as
  "up to four players offline". Stat array has exactly 4 slots.
- **Seat→slot mapping: slot index = join order.** Poke tests in a live
  2P Battle (Kreios Area 1): life i16 at slot 0 block +0x58 := 7 →
  Player 1's HUD panel showed 7; slot 1 := 9 → Player 2's showed 9.
  So Battle Player N = slot N−1. (Campaign remains: slot 0 = AI
  opponent, slot 1 = local human — poke-verified 2026-11-07.)
- **Mode word** at obj+0x55209C: **2 in Battle, 0 in Campaign**
  (mission id alongside: Battle 101 = 100+world*5+area, Campaign 1;
  state word 13 in both).
- Battle life max is 50 (Campaign Kreios L1: 25); the per-slot max
  array at obj+0x3530+slot*2 holds it in both modes.
- **Thorium authority is the per-slot float channel** at stat block
  +0x48 (obj+0x348+slot*0x80). Poking 100.0/240.0/300.0 there moved
  each player's HUD readout independently and the values held
  rock-steady (no drift over 10 s). The u32 pair at obj+0x1B4/+0x1D4
  is a derived shadow: single pokes there revert (the game re-syncs
  it), and in Battle both words tracked slot 1's balance in sampling.
  Inactive slots' float channels sit at 100000.0.
- No human/AI flag exists in the stat block (full 4-block compare,
  Battle 2-human vs Campaign AI+human: the only differences are
  identity/progress/balance fields). Per-player gating therefore
  comes from the mode mapping below, not a flag.

## Design (menu)

The Cheats submenu gains ONE row at the top: **"Player: N"**. It is a
push-button row (opcode 49) that cycles the selected player 1→2→3→4→1;
below it, the four existing toggle rows show and flip **that player's**
toggles. This reuses the stage-2 machinery end-to-end (planted SSO
labels, unlink+rebuild in-place refresh): after a cycle the rebuild's
highlight restarts on row 0 — the Player row itself — so repeated
presses keep cycling, and a toggle flip still lands back on the Player
row. Five rows total, same visual idiom as the game's own option
rows; no new screens, no view overload. The submenu is shared by
Options and the pause Settings entry, so both paths get it.

Glue bridge: `comet_cheats_get/flip(idx)` now operate on the selected
player; new `comet_cheats_get_player()` / `comet_cheats_cycle_player()`
back the selector row. Labels "Player: 1..4" planted at
0x21010120 + player*0x20.

## Design (effects)

`s_cheats[player][toggle]`. The tick maps players to slots by mode:
- Battle (mode word == 2): Player N → slot N−1.
- Anything else: Player 1 → slot 1 (the human), Players 2/3 → slots
  2/3 (inactive in Campaign, so harmless), Player 4 → no slot.
  **Slot 0 outside Battle — the Campaign AI — is never pinned.**

Per-slot pins (formulas all verified in both modes):
- Thorium: float channel obj+0x348+slot*0x80 := amount. The shared
  u32 shadow pair keeps following the slot-1 player exactly as the
  stage-1/2 pin did (Campaign: Player 1; Battle: Player 2), sentinel
  guard retained.
- Stockpiles: i16 ×8 at block+0x64+slot*0x80 := amount.
- Health: life i16 at block+0x58 := max (max array obj+0x3530+slot*2;
  max 0 = inactive seat, skipped).
- Buildables: tech-mask bits 4/5 at block+0x02 per enabled seat.
  **Limitation (by construction):** the requirement table is a single
  shared table — if ANY player enables Buildables the table is
  neutralised for everyone; the per-seat mask bits stay individual.
  The table restores when no player's toggle remains on.

Persistence: control file — legacy lines (`thorium=1`) address
Player 1; new `p2_thorium=1` … `p4_health=0` lines address Players
2–4. Env: legacy `CC_CHEAT_*` seeds Player 1; `CC_CHEAT_P<N>_<NAME>`
seeds Players 2–4. Amounts (`thorium_amount` / `stockpile_amount`)
stay global — one set of amounts for all players.

## Verification (all driven on Xvfb, 2026-11-08)

Menu drive (`cc_drive_s3menu.sh`, Options path, control file =
Player 1 t/b/s on, health off):
- Submenu renders five rows: **Player: 1** (highlighted) / Thorium:
  On / Buildables: On / Stockpiles: On / Health: Off.
- CROSS on the Player row cycles in place: Player: 2 (all Off) →
  Player: 3 (all Off). DOWN+CROSS on Player 3 flips **its** Thorium
  On in place; cycling onward to Player: 4 shows all Off, and
  wrapping to Player: 1 shows the original On/On/On/Off — no
  leakage between players. One CIRCLE after six rebuilds lands back
  on Options with Cheats highlighted (no stacked submenus).
- The pause-menu path opens this same shared builder (the Edit 6
  entry append is unchanged); it was not re-driven for stage 3.

Battle drive (`cc_drive_s3battle.sh`, 2 pads, file = P1 all off,
`p2_thorium=1`):
- Match read (mode=2, mission=101, state=13): slot 0 float
  **550.0** (untouched), slot 1 float **1000.0** (pinned), u32 pair
  1000/1000 (following slot 1 by design), slots 2/3 inactive and
  untouched. The HUD screenshot agrees: Player 1 panel 550,
  Player 2 panel 1000.
- Live-edit phase: file changed mid-match to `health=1` (Player 1);
  the glue's log shows the per-player re-read. Pokes: P1 life := 7,
  P2 life := 9. Four seconds later: **P1 life restored to 50** (its
  health pin), **P2 life still 9** (no pin) — per-player
  independence proven in both directions.

Campaign regression (`cc_drive_s3camp.sh`, the standing control
file: Player 1 t/b/s on):
- mode=0, mission=1, state=13. Slot 1 (human): float 1000.0, piles
  all 60, tech mask 0x0030, u32 pair 1000 — **identical to the
  stage-1/2 behavior**. Slot 0 (AI): piles 0, mask 0, life 25 —
  untouched. (Its +0x48 word is the non-float pattern already seen
  in the block compare; the AI's balance is not the float channel,
  which is precisely why the Campaign mapping never pins slot 0.)

Glue log line confirms the file semantics: legacy lines landed on
Player 1 (`P1 t1 b1 s1 h0 | P2 t0 … | P3 t0 … | P4 t0 …`).

## Ship hunt (status: field not located)

Attempts this session: (1) hunts 2–5's corner-parking diffs chased
spawn timers — the ship likely took no damage in hunts 2/3;
(2) hunt-5's 8 span-capped dumps analysed with diff_death.py over
P3→P4 / P4→P5 / P5→P6 against a P0→P1 control: **no death signature**
(0–2 "restore" hits, all in RSX/high memory; the low-memory 0→1
counters are entity-pool churn). The hunt-5 ship was alive at the
corner through P3 and the match had ended by P7; if it died at all,
only the P6→P7 pair could bracket it, and that pair spans the match
end/loop, so it cannot isolate a respawn. The obj+0x552070 "ship"
word the camera glue reads is 0 in Battle overview — not a live
pointer to follow from outside.

Next experiment (needs its own focused session): drive the ship into
**verified** lethal fire (screenshots bracketing the death within
seconds), with span-capped dumps seconds apart across the death —
the earlier hunts failed on bracketing, not on analysis. When the
field exists, the Ship row joins the submenu as a fifth toggle
(opcodes 57/67 remain free; label slots at 0x210101A0+ are clear).

## Ship hunt 6 (2026-11-08, later): driven matches don't hurt the ship

One more controlled attempt (`cc_drive_ship6.sh`): a 45 s flight
parked the ship deep inside the enemy base cluster in Kreios L1
(camera-follow framed it beside the enemy turret wall), then ~5
minutes of exposure with dumps every ~35 s (Q0–Q8). Screenshots
Q0 (00:38) and Q9 (05:34): the ship is pixel-identical, intact,
never fired on — while the AI actively sent 17+ units and leaked
the idle player base 25 → 13. So at these parking spots the ship
takes no damage at all in a driven match (targeting rules — the
neighbouring weapons are ground-only families — or the exact cell
it settles on). No death occurred, so there is nothing to diff;
the dumps were discarded.

Revised next step: the field hunt needs a session where the ship
provably takes damage — most reliably Jerry's own real play (he
reported ship losses on 2026-11-08): read-only span-capped dumps
from his live process across a death he flags, or a driven setup
that puts the ship in an anti-air weapon's arc once a level's
structure kinds are mapped. Parked until then.
