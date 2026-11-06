# Reconstruction notes 013 — what the kinds are; Thorium; winning; ship death

Date: 2026-11-06. Sources: the game's own language table
(analysis/language/language1.txt, strings 70–100), Pelfast's
public descriptions (Wikipedia / PlayStation Blog / the Comet
Crash wiki), and Stylish-Dark's player-destruction recovery.

## Kind identities (converging-evidence hypothesis)

The language table lists the buildables in menu order — Turret,
Pulsar, Laser, Bomber, Gateway, Basic Ops, Special Ops,
Waypoint, Base — and the units Scout, Tank, Drone, Torpedo
(Basic Ops products, unlocked by upgrades 0/1/2/3) and Hammer,
Thief, Switch, Carrier (Special Ops products). Combined with
the recovered constraints (factories = kinds 26/28; kind 20 is
the constructed Base; kind 27 is no structure), the kind map is:

- 20 Base · 21 Turret · 22 Pulsar · 23 Laser · 24 Bomber ·
  25 Gateway · 26 Basic Ops · 27 Waypoint · 28 Special Ops
- Units: 40 Scout · 41 Tank · 42 Drone (air) · 43 Torpedo ·
  44 Hammer · 45 Thief · 46 Switch · 47 Carrier (48 = a ninth
  deployment-table entry, unassigned)

Weapon targeting from the descriptions: Pulsar = ground only,
Laser = air only ("Good for Thorium gathering"), Bomber = both
with splash, Turret = the basic all-round gun (their "cannon").

## Thorium and winning (public sources)

- Thorium comes from **meteoroids** that cross the field: towers
  (or the ship) smash them to release it; the ship can also grab
  and drag rocks to its weapons. Kills of units pay nothing.
- Units produced by Ops structures are teleported to the home
  base and deployed from there (the transport step of notes 012).
- **A stage is won by sending the required number of your units
  into the enemy base** — offense, not survival. That makes the
  header hypotheses concrete: header[4] (150..650, round) as
  starting Thorium (their synthetic scenarios use 400) and
  header[5] (25/30/50) as the units-to-win target fit both the
  observed values and the documented rules. header[3]
  (60..1200) remains a candidate time limit.

## Ship destruction and reset (from their recovery)

The craft has health; at zero it explodes (sound + particles).
With a base, it **resets there after a countdown of 2** with
health restored and a statistics adjustment; without a base,
death is permanent and the owner's base counter takes −50.
The native sim implements the with-base branch (respawn in 2 s,
full health); the no-base branch and the counter arithmetic
stay open.

## Native milestone 10

- Ship health: enemy tower fire (−10) and unit contact (−8)
  damage the ship; destruction → 2 s countdown → reset at the
  home edge with full health; deaths counted; HUD shows SHIP %.
- Meteors: cross the field periodically and as unmatched script
  events; shooting one down pays 20 Thorium; ordinary kills pay
  nothing (both per the documented economy).
- Offense: player-built Ops (kinds 26/28, now in the build
  cycle) produce their unit bands (26 → 40–43, 28 → 44–47);
  player units march up-field, and each one that reaches the
  far edge scores; reaching the win target clears the stage.
  HUD shows SCORE n/target.
- Starting Thorium and win target read from the header
  hypotheses above when in range.

Tests cover damage/destruction/reset, the offensive win, and
the meteor payout; ctest 5/5; level61 smoke still live.
