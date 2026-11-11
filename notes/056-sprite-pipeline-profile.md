# 056 — Sprite pipeline: where the time goes, the combat scan loop, perf tips

## Ranking (kids' heaviest fight, 12 fps, mission 54 campaign)

Outermost-function attribution of the saved profile (`perf-018`, the binary
that session ran):

| share | what |
|---|---|
| ~15% | job 187280 transform (`j_xform_chunk`) — now two-phase (notes/055) |
| ~22% | job 00188300 (combat), ~13% in one block: `func_00006AA0` |
| ~11% | libc: ~6% lock wait/wake, ~3.7% memmove |
| ~9%  | pool spin (`j_worker`) — now 4 workers |
| ~6%  | 187F00 depth sort (native) |
| ~3%  | the RSX engine itself (vertex remap/fetch, texture hashing) |

The renderer is not the cost; the sprite pipeline is the SPU jobs.

## Job 00188300's target scan — native loop (default on)

Blocks 0x6B88 (unpack next candidate, test the cursor) + 0x6AA0 (distance
from the current point: squared length, frsqest + fi + one Newton step;
keep the nearest passing a bitmask). Each candidate was two block hops and
~78 ops through ctx->gpr. `recomp/scripts/patch_job188300_scan.py`
GENERATES the loop from the lift into the workspace (`job188300_scan.c`):
the lift's helper calls in the lift's order on locals — exact by
construction, nothing game-derived in this repo. Run by setup.py.

`CC_JOB188300_VERIFY=1` (native on a register copy, all 128 registers and
the exit compared at 0x69A0 / 0x6BF8): 3.8M scans, 0 mismatches.
`CC_JOB188300_SCAN=0` restores the lift.

Speed: not measurable in the scripted battle — that scene never runs the
scan (job 188300 is ~3% there, the scan 0%). The kids' fights do; a
monitored session is the test.

## Exact dirty pages from job 187280 (ps3recomp master)

The override claimed the whole LS dirty after every call. It now reports
the pages its workers write (two-phase path). SPURS_JOB_VERIFY: 46.5M jobs,
0 bad pages. No measurable gain: the restage copies (memmove/memset ~5%)
come mostly from other jobs reloading binaries at ~230k jobs/s.

## Scripted-battle profile (95 fps, ~107 draws, ~156k verts per frame)

memmove ~12% (of which ~3.6% SPU DMA in job 001B4300 `C310`, ~2.5% LS
restage), 187F00 ~11%, job 001C9680 ~14% (a two-block loop at 0x200/0x29C:
4x4 transform of 32-byte records into 48-byte outputs — a candidate for the
same generated-loop treatment), SPU channel read/write ~7.7%, memset ~3.5%,
texture change hashing ~3.6% (already sampled + staggered; further cuts
trade detection latency).

## perf in this setup (perf_event_paranoid=2, iloader box)

- Event must be `-e cpu-clock:u` (the default includes the kernel: refused).
- Call graphs need `--user-callchains`: `perf record -e cpu-clock:u -F 149
  --call-graph dwarf,8192 --user-callchains -p PID -- sleep 6`.
- Attach mid-run with `-p` for a few seconds; recording from launch with
  dwarf call graphs is killed before it finishes writing.

## Next

1. A monitored heavy session (combat scan + two-phase transform in play).
2. Job 001C9680's 0x200/0x29C loop: generated native loop, same method.
3. Lock contention (~6% in heavy fights): call-graph attribution in a
   real session.
