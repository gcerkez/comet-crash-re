# 055 — Geometry job 187280: why it was serial, and an exact two-phase split

## Evidence (two monitored kids' sessions, 2026-11-10)

`run-monitored.sh` sessions (~20 min each, `~/cc-perf-work/live/session-*`):
heavy fights fell to 12–25 fps with the GPU 14–20% busy and total CPU
*falling* — a serial chain, not load. Per-thread profiles: one thread did
almost all of `j_xform_chunk` + `j_cull_test` (job 187280's native
override); the pool's 15 workers did next to nothing; combat job 00188300
(lifted, no override) reached ~12% in the heaviest scene.

## Why the pool did not help

`J187280_STATS=1` (scripted heavy battle): ~4,400 calls/s, ~10 groups of
~82 elements each; 99.7% of groups are under the pool's 128-element
threshold. The groups are instances of one mesh (same input vertices,
per-group matrices).

Running a call's groups in parallel is **not exact** (`J187280_VERIFY=1`:
~75% of calls differ). `J187280_MAPW` shows each group's LS writes:
private transform output (01400 + 0x9C0·g), consecutive counters (082xx),
records appended after the previous group's (075xx..), and a shared
scratch at 08400–088DD that every group overwrites — the transform's
16-byte-per-element output, read back by that group's cull.

## The two-phase split (now the default)

1. Across the pool: each group's 0x52C copy, compose (reads only) and
   transform, with the scratch written to a private buffer.
2. In group order: copy the group's scratch into place, then its cull.

0 mismatches over 60,000+ calls (switches and defaults). Workers: 4,
spinning ~200 µs before parking — with 15 parked workers the futex wake
took longer than the work. Per call ~154 → ~100 µs at the same total CPU.
The scripted battle stays at ~86 fps (this job does not bound that scene);
the heavy real sessions are where it should show.

Switches: `J187280_TWOPHASE=0`, `J187280_WORKERS`, `J187280_SPIN`,
`J187280_STATS=1`, `J187280_VERIFY=1`, `J187280_MAPW=N`.

## Next

- A monitored real session to measure the effect in heavy fights.
- Job 00188300 (combat) has no native override; it was ~12% at the peak.
