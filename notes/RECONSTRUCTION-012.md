# Reconstruction notes 012 — mission setup, recovered from the resources

Date: 2026-11-05. Jerry's direction: check the resources for the
mission-setup parts we don't have (our own mission-setup hub in
the US build is the fragmented function of notes 010 — its EU
counterpart's behavior is what the decomp branches recovered).

## What the resources hold

All from Stylish-Dark's newest branch (their PPU addresses are
the EU build's; the data tables transfer):

- **Per-kind initial levels** (map post-constructor transition):
  kinds 20–24 → 1, kind 26 → 2, kinds 25/28 → 5; kinds 27/29 are
  not structures at all.
- **Settlement rule**: a pre-placed map structure is imported
  (settled, active) iff its stored upgrade level is at or below
  its kind's initial level. In our record layout the stored level
  is **raw[1]** — consistent with everything observed: raw[1]
  spans 1..6, the only kind-25 record carries 5 (its initial
  level), kind-26 records span 2..4 against an initial level of
  2 (the 3s and 4s wait for construction).
- **Event semantics refined**: a map event *allocates a new
  structure* — construction, not instant activation — funded and
  completed through the same timed construction flow as player
  builds. (Their scenario shows a map event debiting a player's
  resources; whose purse an enemy script draws on is not yet
  pinned down, so the native sim does not charge for script
  builds.)
- **Factories are kinds 26 and 28** (factory commands pair event
  selectors 4..8 with those types) — milestone 8's kind-24 guess
  was wrong and is corrected.
- **Production → deployment**: factories reserve pending orders;
  units deploy from the owner's **base** on a per-kind release
  clock (recovered table: produced kinds 40–43 ≈ 0.05 s,
  44/46 = 0.5 s, 45/47/48 = 1 s, at full input strength). The
  original transports factory output to the base before
  deployment; opponents deploy automatically, players hold a
  control.
- **Players**: up to 4, each with a base cell, team, and a
  resource pool with a max. Mission setup is per-player state,
  not a single global pool.

## Native milestone 9 — mission setup implemented

- `arena_sim` settlement now follows the recovered rule:
  structure records with raw[1] ≤ initialLevel(kind) start
  active; the rest start dormant.
- Script events start *construction* (build timer) of a dormant
  structure of their kind instead of flipping it on; unmatched
  events still send a comet.
- Factories are kinds 26/28: they stockpile pending units on a
  production timer and deploy them from the factory cell on the
  recovered per-kind intervals, cycling produced kinds 40..47.
- Effect on real data is immediate: on level61 the settled
  kind-26 factories now deploy ground units from the first
  seconds, towers engage, and the base takes hits — the match
  develops like the original's instead of waiting for the script.

## Still open

- Factory→base transport and base-anchored deployment (needs
  identified base records; kind 20 is the constructed base in
  their scenarios but never appears as a placed record in our
  maps — bases may be placed by the setup hub from header data).
- Player-side deployment control (hold-to-release at the base).
- Starting resources per player (their scenario values are
  synthetic; our header fields [3]/[4] remain candidates).
- Ship damage and the win/lose structure around base counters.
