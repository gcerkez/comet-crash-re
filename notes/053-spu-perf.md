# 053 — SPU job cost: dirty-page staging and the depth-sort override

Date: 2026-11-09/10. Fork branch `perf/spu-mem` (off `muse/savestates`
plus SPU_DIRECT), repo branch `muse/savestates`; both merged to master/main. RUNTIME_PATCHES #34,
#35. Tooling: `recomp/perf/`.

## Request

Jerry: the game gets slow under heavier sprite loads and quickly
unplayable; find more efficient ways to handle SPU writes and memory
flows. Continues notes 048 (per-draw shader walks) and 050 (the 187280
geometry override, and "the next wall").

## Result in one table

Driven 2-player Battle (Kreios area 1, mission 101), windowless on the
RX 7900 XTX, uncapped, one switch changed at a time on the same binary:

| configuration | fps | cores | switch used for the A/B |
|---|---|---|---|
| old staging, lifted 187F00 | 75.4 | 5.24 | `SPURS_JOB_FULLCOPY=1` |
| + dirty-page staging (#34) | 92.7–93.0 | 4.38–4.43 | (default) |
| + 187F00 native override (#35) | 100.8 | 4.01 | vs `PS3_NO_JOB187F00=1` |

**Not confirmed: Jerry's heavy scene.** The driven Battle never gets
slow (75–100 fps), so these are measured CPU reductions in the code the
heavy scene also runs — not a demonstrated fix for the unplayable case.
His session with `run-spuperf.sh` is the verdict.

## Getting a trustworthy measurement (most of the first hours)

Four traps, each of which produced plausible-looking wrong numbers:

1. **A window on Xvfb means llvmpipe.** With `PS3RECOMP_VK_WINDOW=1`
   under Xvfb the backend picks device 1 (llvmpipe): ~10 fps at ~10
   cores, 80% of samples in JIT code. Measure with
   `PS3RECOMP_VK_WINDOW=0` and check the log says
   `using device 0: AMD Radeon RX 7900 XTX`.
2. **A window on the real desktop is throttled** when the monitors
   sleep (the compositor stops presenting): ~20 fps at 1.6 cores.
3. **Windowless menus run fast**, so an 8-report d-pad hold
   auto-repeats: two DOWNs overshoot Battle and land on Online Scores,
   which opens the PSN dialog (auto-answered NO) and leaves the game on
   the main menu. Tap with a count of 2 and gate on guest state
   instead of sleeping (`obs.py`: state 11 = menus, 13 = match;
   mode 2 = Battle, 1 = Training, 0 = Campaign).
4. **`build-spuperf` was configured without Vulkan**
   (`PS3RECOMP_RSX_VULKAN=OFF`) and silently ran the null RSX backend:
   the guest reaches state 13 but there are no `RSX_PRESENT` lines at
   all. Also: the runtime's source glob has no CONFIGURE_DEPENDS, so a
   new fork file needs a manual `cmake` re-run.

Profiler: `perf` in the iloader box, `-e cpu-clock` only. Hardware
counters, attaching to a running pid, and call graphs are all refused
(perf_event_paranoid=2), so the game is launched under
`perf record -D <delay>` and the report is flat symbols.

Fork housekeeping found on the way: `master` carried the SPU_DIRECT
commit on a base that lacks the 187280 override, FP memo, audio and
savestate work (those live on `muse/savestates`), plus uncommitted
copies of the FIFO-resync commit and the MFC fast-path WIP. The
binaries Jerry runs were built from a combined tree. `perf/spu-mem`
recreates that tree in git: `muse/savestates` + SPU_DIRECT (0b54109f)
+ FIFO resync (f8f6068b from origin) + the work below. master's
uncommitted state is kept in a stash ("pre-perf: master WIP").

## Finding 1 — staging, not running, was the biggest SPU cost (#34)

Flat profile of the baseline Battle: `__memmove` 16.1% +
`__memset` 13.3% = 29.4% of all samples, more than any job's code.

`spu_run_spurs_job` (runtime/spu/spurs_job.c) staged every job by
memsetting the whole 256 KB local store and copying the job binary in.
A counter added for this (`SPU_JOBSTAT=<secs>`) shows what that means
in a Battle:

| image | job | jobs/s | binary |
|---|---|---|---|
| 6 | 00188300 entity simulation | ~90,000 | 177 KB |
| 8 | 001B4300 | ~100,000 | 74 KB |
| 5 | 00187F00 depth sort | ~12,000 | 928 B |
| 7, 11 | 001B3800, 001C9680 | ~15,000, ~12,500 | small |

About 230k jobs a second, each with a 256 KB memset, and the two
hottest also a 177 KB / 74 KB copy.

**First attempt, rejected by measurement:** scan each page and memset
only non-zero ones; memcmp the binary and copy only differing pages.
Correct, but the scans (`memcmp` 9.8% + the zero scan 8.9%) cost what
they saved: 78 fps either way. At this rate the store is not
cache-resident, so reading it is as expensive as writing it.

**What works: track the writes.** `spu_context.ls_dirty` is 64 bits,
one per 4 KB page, set by every path that writes local store for
guest code — `spu_ls_write128/32` (the only store forms lifted code
uses: 4,064 call sites, zero direct `ctx->ls[...] =`), MFC GET and the
null-DMA memset, the GETLLAR line copy, two direct writers in
spu_channels.c, the mp3 codec, and the runner's own staging. Between
jobs the invariant is: a clean page holds the previous binary's bytes
(below its size) or zero (above). The next job rewrites dirty pages
only, and re-copies the binary only if its EA/size changed or the
"binary epoch" moved.

The epoch is the one semantic decision: it is bumped per JOBLIST batch
(after the batch's binary has been fingerprinted from guest memory)
and per one-off dispatch, so a resident binary is trusted only within
a batch. jm2 behaves the same way (it does not reload a resident
binary). A title that rewrote a job binary mid-batch would differ;
`SPURS_JOB_FULLCOPY=1` restores the full memset+memcpy.

The 187280 override's pool workers store through the same context
concurrently, so their bit updates can race; it claims the whole store
dirty on exit instead.

Verification: `SPURS_JOB_VERIFY=1` compares every staged store with
the reference (binary bytes from guest memory, zero above): 25.9M jobs
across boot, menus and a live Battle, 0 bad pages. Standalone:
200k random job sequences (same/different binary, random marked
stores, binary rewrites between batches, full-store claims), 0
failures.

Effect: memset+memmove 29.4% → 11.7% of samples; 75.4 → 92.7 fps;
5.24 → 4.43 cores.

## Finding 2 — the depth sort (#35)

After #34 the top symbol was job 00187F00 (~23% of samples for a
928-byte program). Its work function (guest func_00000160, called from
func_00000054) is three loops over 1024 elements:

1. `func_000001B0` — position (x,y,z,1) of each 32-byte record times
   four matrix rows; only lane 2 (z) of the result is kept, written
   with the element index into a 16-byte entry.
2. `func_00000230` — a gnome sort of the entries by that float,
   descending, **capped at 10240 steps**. 1024 elements need far more
   in the worst case, so the cap is observable: a capped run leaves a
   partial order, and the override must reproduce exactly that.
3. `func_000002BC` — gather the records into sorted order.

`runtime/spu/spu_job187f00.c` transcribes the loops. Arithmetic uses
the fork's own per-lane exact helpers (`spu__fm_any`, `spu__fma_any`,
`spu__fa_any`), applied only to the one lane the job keeps — the same
operations on the same operands, a quarter of the float work. It
handles a call only when the four buffers are aligned, inside LS and
pairwise disjoint, and returns 0 otherwise so the lifted function runs
(the lifted loops interleave their reads and writes, which only
matters under aliasing). Seam: the 187280 pattern — a patch script
swaps the callee at the one call site for a glue function with a kill
switch.

Verification, in increasing order of trust:

- Harness (`recomp/perf/harness187f00/`): 111 cases — random, sorted,
  worst-case, nearly sorted, raw-bit keys (NaN/denormal/inf), several
  placements, overlap and out-of-LS layouts that must take the
  fallback — comparing the full 256 KB LS plus live registers, in
  trampoline and direct-tail-call modes. 0 failures.
- The harness can fail: six deliberate mutations of the native (step
  cap, compare direction, index halfword position, gather index,
  preserved entry bits, wrong matrix row) are each caught (48–90 of
  111 cases).
- Live: `CC_JOB187F00_VERIFY=1` runs the native on the real context
  and the lifted function on a private copy of the pre-call context,
  then compares all of LS. 1.36M real calls in a driven Battle: 0
  mismatches, 0 declined — so the disjoint-buffer assumption holds for
  the game's own layout.

Registers gpr[2..79] differ from the lifted chain after the call; they
are dead there (func_00000054's tail reassigns gpr[6..9] and reads
gpr[80..82]).

Effect: 4.2–6.4x per call in the harness; in the Battle 93.0 → 100.8
fps, 4.38 → 4.01 cores.

## Measured and found not to matter

- The MFC fast paths (bitmap span test for PUT reservations, one gate
  for ~25 per-DMA diagnostics; RUNTIME_PATCHES #16 items 1–2) and the
  AVX2 helper build (#16 item 3): correct, now compiled and running,
  no measurable frame-rate change in this scene (~78 vs ~77 fps).
  Kept as behaviour-neutral.

## Switches

| env | effect |
|---|---|
| `SPURS_JOB_FULLCOPY=1` | old staging: full memset + binary copy per job |
| `SPURS_JOB_VERIFY=1` | check every job's staged store (slow) |
| `SPU_JOBSTAT=<secs>` | per-image jobs/s and staged bytes |
| `PS3_NO_JOB187F00=1` | lifted depth sort |
| `CC_JOB187F00_VERIFY=1` | native vs lifted on every depth-sort call (slow) |
| `PS3_NO_JOB_OVERRIDE=1` | both job overrides off (187280 and 187F00) |

## Reproducing

```
recomp/perf/heavy_drive.sh build-spuperf run1 200 SPU_JOBSTAT=10
STICK_FROM=50 PERF_AT=120 recomp/perf/heavy_drive.sh build-spuperf run2 200
```

Drives menus → 2P Battle by guest state, samples fps/CPU every 10 s
into `~/cc-perf-work/runs/<tag>/fps.txt`, optionally wraps the game in
`perf record`. Cheats (thorium, stockpiles, health) are on; pad 1 holds
the right stick from `STICK_FROM` seconds.

## Still open

- Jerry's heavy session (above). If it is still slow there, capture
  `SPU_JOBSTAT=10` output and a perf sample from that scene: the job
  mix under real unit spam may differ from the driven one.
- The override is still ~13% of samples, nearly all in the
  exact-emulation float helpers and the sort itself.
- 00188300 (entity simulation, ~90k jobs/s) remains the largest body
  of lifted code; notes 050 found no clean inner seam.
- Remaining memmove (~10%) is job input staging and DMA — real data
  movement, not re-staging.
- The 187280 glue (`job187280_glue.cpp`) is still a workspace-only
  file; the 187F00 glue is tracked in `recomp/`.
- Merged 2026-11-10: fork `perf/spu-mem` into fork `master`
  (a08b5377), this repo's `muse/savestates` into `main`; the submodule
  names the fork merge. Numbered 053 because main already had a 052
  (the call-path study, notes/052-call-path-fast-path.md).
