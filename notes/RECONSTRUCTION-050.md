# 050 — Native override for the geometry SPU job (design)

**Date:** 2026-11-08. **Status:** design; implementation not started.
**Decision (Jerry, 2026-11-08):** native-override the hot SPU job in
the recomp rather than pivoting to the full native port. The hand
port remains the long-term / Quest path.

## The job

`spu_job_00187280` (guest EA 0x187280, size 0xC20; workload image 4;
fingerprint 0x552BED5350500A0B; registration
`spu/spu_workloads.c:47-48`). First job in an 8-job SPURS JobChain
built once at init (guest FUN_0016c5e0 from FUN_000d9630; module
table at guest 0x1F7724) and kicked + awaited every frame by
FUN_0011cda0 via FUN_0016b91c / FUN_0016b8b4, inside the
scene-submission path (live stack: `cellSpursEventFlagWait` ←
FUN_0016B8B4 ← FUN_0011CDA8 ← FUN_000FC1A0 scene_submitDraw).

Work (identification pass, 2026-11-08; citations in the job's
`spu_recomp.c`): per-group matrix composition from a 64-byte group
record × a shared 64-byte block (func_00000538); per-element vertex
transform + normal normalize over 32-byte elements (hot loop
func_00000670); a dot-product pass (func_000009A0, flag-gated); a
triangle frustum-cull pass over a u16 index stream (func_000007DC →
func_00000820, divide refinement func_00000A70). Cost = groups ×
elements/group — linear in scene geometry; in a heavy Battle it
burned ~74% of a host core and the main thread waits on it.

## Correction to the identification pass

The first pass claimed the job "issues only MFC GETs; results stay
in the chain work area". **That is inverted.** In this fork (and in
the Cell MFC encoding) command **0x20 is PUT and 0x40 is GET**
(`runtime/spu/spu_dma.h:141-149`). The job issues **no GETs**: its
inputs are staged into LS by the runtime before entry (below), its
three chunked loops (func_000002C0 / 00000370 / 00000418, ≤16,384 B
chunks, 4/8/16-byte tails) are **write-back PUTs** to
descriptor-derived guest EAs, and there is **no runtime copy-back
path** anywhere in the job machinery. The override design below
follows the corrected contract.

## The seam

Every execution path funnels through **`spu_run_spurs_job()`**
(`runtime/spu/spurs_job.c:167`): the chain walker via
`spu_workload_dispatch_job` (`runtime/spu/spu_workload.c:1104`) and
the JOBLIST fast/fallback paths (`libs/spurs/cellSpurs.c:3109` /
`:3111`) all call it. Hook at its top, after the
`if (!entry || !job_ea) return -1;` guard (:170) and before TLS
context acquisition (:191-220):

```c
/* returns nonzero if the job was fully handled natively */
extern int ps3_spu_job_override(int image_id, spu_lifted_entry_fn entry,
                                uint32_t job_ea, uint32_t job_desc_size);
if (ps3_spu_job_override(image_id, entry, job_ea, job_desc_size))
    return 0;   /* same success code as a completed lifted run */
```

- **Identity:** entry-pointer comparison against the glue's
  `spu_job_00187280_spu_func_00000000` (image_id 4 as a secondary
  check). Follows the MP3-codec precedent: runtime code calls a
  title-provided `extern` host function; the runtime keeps a stub
  (cf. `runtime/spu/spu_tsp_weak.c`).
- **Kill switch:** env `PS3_NO_JOB_OVERRIDE=1` restores the lifted
  job (pattern-consistent with `PS3_NO_HOST_CODEC=1`).
- **No `spu_context` needed.** Everything the job can observe is
  derivable from `job_ea` in guest memory, and all its effects are
  guest-memory writes.

## The contract the override must reproduce

**Inputs (as staged by `spu_run_spurs_job`, `spurs_job.c:259-337`):**

- Descriptor copied verbatim to LS top (cap 1,024 B): 48-byte
  `CellSpursJobHeader`, then the DMA input list at `job_ea + 0x30`
  (BE u64 entries: size in bits 32..46, EA low in bits 0..31), then
  the cache list (BE u64 `(size<<32)|eaLow`). Header fields:
  `sizeDmaList` +0x0A, `eaHighInput` +0x0C, `useInOutBuffer` +0x10,
  `sizeInOrInOut` +0x14, `sizeOut` +0x18, `sizeStack`/`sizeScratch`
  +0x1C/+0x1E (`spurs_job.c:80-95`).
- ioBuffer = the input list's buffers concatenated in order, each
  16-byte aligned; cache buffers each in their own 1,024-aligned
  region; oBuffer aliases ioBuffer iff `useInOutBuffer`, else its
  own region sized `sizeOut`.
- Entry registers (synthesized context, `spurs_job.c:303-337,
  425-431`): r3 = LS ptr to a `CellSpursJobContext2` (ioBuffer ptr
  +0x00, cacheBuffer[4] +0x04, sizeJobDescriptor +0x14, numIo +0x18,
  numCache +0x1A, oBuffer +0x1C, scratch +0x20, DMA tag 20 +0x24,
  descriptor EA +0x28); r4 = LS ptr to the descriptor copy, whose
  user area begins at +0x30. The job's func_00000084 unpacks its
  argument registers from these; func_000004A0 saves them as the
  work parameters (r4→group count, r5→element count, r7→triangle
  count, r6→flags, r8/r9/r10/r11/r12/r14/r15→buffer cursors and the
  shared block pointer).

**Outputs:** exactly the job's PUT writes — `memcpy(vm_base+ea,
ls+lsa, size)` per chunk (`spu_dma.h:719, 726`) across the three
phases, targets computed in the job's argument unpacking. Which
descriptor user field feeds each PUT cursor is the remaining
field-by-field derivation (Phase B, step 1) from func_00000084 /
func_0000029C / func_000004A0.

**Completion bookkeeping (from `spu_run_spurs_job`'s tail,
`spurs_job.c:475-481`):** clear the thread-local mailbox state
(`g_spurs_job_mbox`, `g_spurs_job_mbox_intr`,
`g_spurs_job_mbox_valid` = 0 — this job posts no mailbox) and set
`g_spurs_job_cmd` as the tail computes it (descriptor +0x4C →
pointer → >>16; 0 unless populated). Chain-level completion
(`jc_signal_done`, counters) is the caller's and is unaffected.
Return 0.

**Known side channel:** `spu_run_spurs_job` retains the finished
job's LS in `s_last_job_ls` for `sys_spu_thread_read_LS` polls
(`spurs_job.c:452-457`). An override produces no LS. Mitigation if
needed: point it at a zeroed buffer on the override path. Whether
this title polls the chain's LS is undetermined — flagged for the
verification phase, not assumed.

## Math exactness

The override must reproduce the lifted job's arithmetic
element-for-element, including the SPU estimate semantics the job
relies on: `spu_frsqest` + `spu_fi` Newton refinement in the
normalize, `spu_frest` + `spu_fi` in the cull divide, and the
fm/fma composition chains. Per Jerry's standing truth rule, the
decompiled code defines correct output; "close" is a defect. Speed
comes only from *where* the identical math runs: batches split
across a host thread pool (16 threads available), elements within a
batch processed in order per output stream (outputs are disjoint
per group — group outputs write to per-group cursors, so groups
are the parallel unit; verify cursor disjointness in the field
derivation before relying on it).

## Verification plan (before the override may default on)

1. **PUT-stream equivalence:** instrument both paths (lifted and
   override) behind a debug env to log every output write as
   (EA, size, FNV hash of bytes) per invocation; drive identical
   scripted scenes (menu → Campaign match → Battle) and diff the
   streams. They must match exactly, in order.
2. **Visual identity:** screenshot pairs from the same driven
   scenes, override on vs off — frames must be identical (the
   menu-frame md5 discipline from notes 042/043).
3. **Performance:** the 60 s RSX_PRESENT frame-field protocol on a
   driven scene, plus Jerry's heavy-Battle session as the decisive
   test (driven scenes are lighter than his).
4. Only then: default on (kill switch retained), fork commit,
   RUNTIME_PATCHES entry, push on Jerry's word.

## Remaining work

- Phase B step 1: field-by-field derivation — descriptor user area
  → argument registers → the three PUT cursors and the io/cache
  buffer layout the compute reads. Anchors: job func_00000084,
  func_0000029C, func_000004A0, func_00000538, func_00000670,
  func_000007DC/820, func_000009A0, func_00000A70, and the staging
  walk in `spurs_job.c:259-337`.
- Phase B step 2: the override TU (fork `runtime/spu/`), the seam
  patch, the weak stub, the glue registration.
- Phase C: verification per above. Phase D: commits + docs.

## Addendum (2026-11-08): design revision — override the inner call, not the job

Reading the job's CRT unpacker (func_00000084) settled a strategy
question. That unpacker derives the work function's argument
registers from the descriptor and context through ~150 shuffle /
rotate / multiply operations (rotqby extractions keyed on the
context pointer's low bits, 64-bit EA products). Re-deriving those
fields independently in host code is the most error-prone possible
route, and it duplicates logic the game already runs correctly.

**Revised seam: intercept the call to the work function, exactly
the MP3-codec pattern applied one level down.** The lifted job's
func_0000029C calls the work function directly:

```c
ctx->gpr[0] = spu_link(0x2B0);
{ host_depth++; spu_job_00187280_spu_func_000004A0(ctx);
  spu_drain_call(ctx, 0x2B0); host_depth--; spu_img_restore(...); }
```

The patch (assert-anchored, on the generated
`spu/spu_job_00187280/spu_recomp.c` in the title workspace, same
discipline as `patch_lift_cheats.py` on the PPU lift) replaces only
the callee expression:

```c
{ host_depth++; ps3_job187280_compute(ctx);   /* native or fallback */
  spu_drain_call(ctx, 0x2B0); host_depth--; spu_img_restore(...); }
```

`ps3_job187280_compute(ctx)` (title glue, extern; runtime/workspace
keeps a fallback that simply calls the lifted function) reads the
argument registers from the live `spu_context`, performs the
compute natively over the job's LS buffers, and returns — the
lifted job then runs its own PUT write-back phases, its own
epilogue, and the runtime performs all staging, mailbox and chain
bookkeeping exactly as today. `PS3_NO_JOB_OVERRIDE=1` forces the
fallback. What the whole-job hook (previous section) would have
had to reproduce — descriptor staging, io/cache layout, mailbox
tail, `s_last_job_ls` — is all inherited for free. The whole-job
design above is retained as the fallback if the inner call proves
unsuitable.

### Register contract at the work-function call (derived)

From func_000004A0's prologue and the callees (all in the job's
`spu_recomp.c`):

| Arg | Saved as | Role |
|---|---|---|
| r3 | (reworked in prologue) | descriptor-derived base for 64-bit cursor products |
| r4 | gpr[54] | **group count** (outer-loop bound, func_00000940) |
| r5 | gpr[27] | **elements per group** (inner-loop bound, func_00000670 / 000009A0) |
| r6 | gpr[55] | **mode flag**: ≠0 → transform pass; 0 → dot-product pass. ANDed with r13 → gpr[58], the per-group +0x30 copy gate |
| r7 | gpr[41] | **triangle index-count bound** for the cull pass (func_00000918) |
| r8 | gpr[60] | input element stream LS base (word 0) |
| r9 | gpr[34] | index stream LS base (cull pass fetches) |
| r10 | gpr[23] | group-record stream LS base (records 64 B apart) |
| r11 | gpr[62] | 32-byte output stream base (+ cumulative element offset ×32) |
| r12 | gpr[61] | cull output record base (+ 4 × running index base) |
| r13 | — | flag bits (see r6) |
| r14 | gpr[44] | per-group +0x30 copy output cursor base (+16/group) |
| r15 | gpr[42] | shared 64-byte block LS pointer (matrix composition) |

Plus CRT-computed state the compute reads: gpr[18] word 0 = the
16-byte output stream base (transform positions / dot results,
func_00000538 tail and func_0000095C), and the shuffle constants at
LS 0xB50–0xB70 (the job's own data tail).

### Compute structure (what the native code transcribes)

Per group (func_0000052C / 00000538): optionally copy the group
record's +0x30 quadword to the gpr[44] stream; compose two 4-row
transform sets from the 64-byte group record × the shared block
(shufb de-interleave + fm/fma DAG — transcribe literally).
Then exactly one of:

- **Transform pass** (r6≠0, func_00000670): per element, gather
  position (bytes +0..+12, w:=1.0) and direction (+16..+28, w:=0)
  from the 32-byte input record, read the group's scalar at record
  +60, apply the composed transforms, normalize the direction with
  the SPU rsqrt sequence (frsqest → fi → fm/fnms/fma Newton step),
  write 16 B to the gpr[18] stream and 32 B to the r11 stream.
- **Dot pass** (r6==0, func_000009A0): per element, same position
  gather, one broadcast dot product against the second transform
  set, write 16 B/element to the gpr[18] stream.

Then, if r7>0, the **cull pass** (func_000007DC / 00000820 /
000008E0 / 00000918, refine path func_00000A70): walk the u16 index
stream 3 at a time, fetch transformed positions from the gpr[18]
stream, run the ±w clip tests (fast path on w; divide-refined path
via frest in func_00000A70), and write kept triangles as 12-byte
records to the r12 stream with the running element base folded in.

Outputs are disjoint per group (all output cursors advance by
per-group counts: elements ×16/×32, indices ×12, copy ×16), so
groups are the parallel unit. Post-call register consumers in
func_0000029C's continuation are limited to the PUT cursors
(gpr[81]/gpr[84] family, set before the call and not written by
the compute) — the checklist for implementation is to confirm no
post-call code reads a compute-clobbered register other than
through LS.

### Exactness rule for the transcription

The native compute transcribes the lifted DAGs operation-for-
operation, using the fork's own `spu_*` helper semantics
(`runtime/spu/spu_helpers.h`) for the estimate instructions
(frsqest, frest, fi) and the fm/fma/fa/fnms chains — no
"mathematically equivalent" simplifications. Verification is the
PUT-stream equivalence diff from the plan above (which, under this
design, compares the final guest-memory writes of full job runs,
override on vs off — an even stronger end-to-end check).

## Completion (2026-11-08): built, harness-verified, live-verified

**Implementation.** `runtime/spu/spu_job187280.c` in the fork
(entry `ps3_job187280_compute_native`) is a mechanical transcription
of guest funcs 0x4A0–0xB00 on a private register copy, using the
fork's LUT-exact spu_frsqest / spu_frest / spu_fi helpers; it never
writes ctx->gpr. The seam is the inner call site in the generated
job TU: func_0000029C's call to func_000004A0 is swapped for the
workspace glue `cc_job187280_compute(ctx)` (job187280_glue.cpp),
which dispatches to the native function — or to the lifted one when
the kill switch PS3_NO_JOB_OVERRIDE is set (non-empty). The glue
also performs the exit-state writes the lifted chain guarantees
(ctx->pc from gpr[0]; g_spu_trampoline_fn = 0) — that contract was
found by the harness, below. The patch is applied by
recomp/scripts/patch_job_override.py (marker "JOB187280 OVERRIDE",
assert-anchored, per-file idempotent; re-run after any SPU
re-lift), and setup.py runs it as part of the standard pipeline:
**the override is on by default**; the generated TU plus the fork
file are all a fresh build needs.

**Parallelism rules (measured, not assumed).** Groups run
SEQUENTIALLY: the 16-byte output region is per-group scratch, and
gpr[43]/[53]/[45] carry across groups. Within a group, the element
passes fan out (disjoint outputs; cursors affine in the element
index). Culling splits into a parallel TEST phase and a SERIAL
write phase — the write helpers merge through a loop-carried base
(gpr[13] through func_00000B00 into later func_000008E0 writes) and
neighbouring 12-byte records share 16-byte LS words, so the writes
must stay ordered. The TU's thread pool (workers = min(cores-1,
15)) engages at >= 256 elements; culling parallelises at >= 48
triangles with r7 <= 30000; serial fallbacks otherwise and on
allocation failure.

**Equivalence harness — PASS.** The harness builds the real lifted
job TU + the native TU + the glue, runs both on identical
spu_context copies, and byte-compares the full 256 KB LS plus the
live registers (gpr[0..1], gpr[80..127]). Grid: groups
{0,1,2,3,5,8,13} x elems {0,1,2,5,16,33} x r7 {0,1,2,3,7,12,31,64}
x mode {0,1,2} x r13 {0,1,3} = 3,024 cases + 8 large + 1 wrapper =
**3,033 cases, 0 failures, three consecutive runs**. Five defects
were found and fixed in the TEST RIG along the way, none in the
transcription: (1) a thread-pool livelock (workers must park when
the head job is fully chunked); (2) SPU_DIRECT does not call its
target — it sets g_spu_trampoline_fn and the runtime's
spu_drain_call pumps it, so the harness had to drain identically;
(3) spu_ls_read/write128 mask addresses to 256 KB — synthetic
regions >= 0x40000 silently alias; (4) extreme synthetic shapes
alias cursors across groups in ways the real game never issues
(grid capped at groups*elems <= 2048); (5) random cull indices over
the whole LS made serial results interleaving-dependent (indices
restricted to valid range, as the game's own streams are).

**Performance.** Per call, 200 reps: transform-heavy shapes
2.27x / 2.75x / 3.09x faster native; cull-heavy 1.11x; tiny
batches a wash (0.94x). Real-GPU A/B in a light Campaign match:
74.0 fps native vs 75.0 fps lifted, CPU 476 vs 462 jiffies/s — no
regression. In Jerry's heavy cheat-fed Battle, stack sampling
afterwards found **zero samples in the overridden job**: the
bottleneck moved wholesale to the entity-simulation job
(00188300, below) plus the smaller jobs. At extreme cheat-fed
density that session's frame tore once (giant stretched geometry,
HUD intact); the override audit is clean, and Jerry's hypothesis —
the guest code overflowing its own buffers beyond supported unit
counts — fits; the discriminator (same density with
PS3_NO_JOB_OVERRIDE=1) has not been run.

## The next wall: SPU job 00188300 (entity simulation)

Identification (2026-11-08): not a kernel — a complete 177 KB SPU
program, the arena entity simulation (it also owns ship
damage/heal/death and the unit counters' maintenance; see notes
049 addendum). 112 subroutines, a dispatcher (main, guest
0x25450) walking entity records and calling ~30 specialised
workers through a function-pointer table built at LS 0x26200,
bidirectional inline DMA, state persisting across invocations.

A full control-flow reconstruction (115 disjoint direct-edge
regions, census verified against the TU) gives the seam verdict:
**there is no 187280-style inner call to swap.** The hot block
0x24B50 is inside the dispatcher itself (helper_24B20 — tag sync,
its own PUT, persistent-state R/W, computed dispatch): the
control plane, not a candidate. Worker 0x1C2E0 is reachable only
by computed dispatch and its guest-memory effects flow
transitively through three levels of callees. The ONLY plausible
seam is the shared worker-family utility 0x61F8 (hot block
0x66A8; 10 static call sites across the worker family), and only
as a compute override that keeps invoking its lifted DMA helper
(0x708) — gated on a caller-attributed runtime profile first,
because 0x61F8's measured heat is the sum of its 10 callers and
its in-region vs callee split is unknown. Fallback lever:
optimising the recomp's SPU execution of this image generally.


## Addendum - pool tuning (2026-11-08 evening)

The override's weak bench shape was 16 groups x 128 elements with
r7=1500 (cull-heavy): only ~1.04x the lifted job on a contended
machine. A decomposition bracket (pool off / parallel cull off /
fully serial) located the cost: pooled-vs-serial cull tests are
worth only ~0.6 ms there, while the shape's 128-element groups fell
below the transform pool's 256-item floor, so all 16 groups'
transform passes ran serially on the calling thread (~1.5 ms of
serial work per call). Fix (fork 311a45a7): element-pass pool
threshold 256 -> 128, one chunk per worker for both passes, and
short spin-then-park waits on the pool (256 worker pauses / 1000
caller pauses). Long spins are a trap under contention: at a few
thousand iterations they regressed every shape (cull-heavy 6.98 ms,
transform shapes +22-25%) because 15 spinning workers steal cores
from the caller's serial phases. Result (native avg per call,
medians of 6, contended host, game running): cull-heavy 6.32 ->
4.49 ms (-29%); g=8 e=256: 2.49 -> 2.51 (flat); g=4 e=300: 1.16 ->
1.11; g=1 e=511: 0.41 -> 0.41; g=2 e=64: 0.19 -> 0.18. Equivalence:
0/3033 failures, three consecutive runs, harness built from the
final TU (byte-identical to the committed fork TU).
