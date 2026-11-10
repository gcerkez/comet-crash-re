# 052 - The SPU call path: a compiled-out fast path

## The question

Three rounds of caller-attributed profiling of SPU job 00188300
(notes 050, profile addenda) found no native-override seam: the
cost is a deep per-record pipeline of lifted calls. The one
cross-cutting number was ~125 cycles of call-wrapper overhead
per call at the 0x61F8 seam, across what was assumed to be ~2M
trampoline hops per Battle window. This study priced the call
path itself.

## The premise, corrected

Both numbers were wrong.

- Transfers: job 188300 executes ~378M SPU_DIRECT transfers per
  match window (378.4M Battle / 376.2M Campaign, global counters
  on the macro and both drain pumps). The ~2M figure was a
  single seam's call count (0x61F8: 1.32M). Per worker dispatch:
  ~207 transfers, ~25 lifted-call brackets. Most transfers are
  basic-block edges - the lifter splits at block boundaries -
  not call sites.
- Cost: a drain-pumped hop is ~14 cycles gross (microbench on
  the fork's own test_spu_direct recipe); the fast path saves
  ~6.5-8.5 cycles net per transfer in situ. The 125-cycle figure
  priced the nested rdtsc timer pair itself plus cache-cold
  paths in the instrumented build. Cautionary measure: the
  counting instrumentation alone inflated per-dispatch worker
  cycles 33.6K -> 56.9K via hot shared atomic cachelines;
  counting runs were used for counts only, never cycles.

## The discovery

The fork already ships the fix. SPU_DIRECT can chain calls
directly with musttail tail calls (SPU_DIRECT_TAILCALLS), but
the compile-time gate was Clang-only:

    #if defined(__clang__) && __has_attribute(musttail)

The game builds with GCC (16.2.1), which has musttail - the gate
just never enabled it. Every shipping build ran with the fast
path compiled out; the boot log says so: "[spu] direct tail
calls off (no musttail in this compiler)". The change is +7
lines in runtime/spu/spu_context.h: an #elif defined(__GNUC__)
branch defining SPU_DIRECT_TAILCALLS under the same
__has_attribute(musttail) test. No new runtime logic.

## What a fast path has to preserve (contract, from fork sources)

Per transfer the trampoline pump guarantees: (1) bracket
termination isolation - a nested drain ends exactly at
pc == return_pc, with drain_ret_pc saved/restored across
nesting; (2) per-step ctx->steps++, gating LS-0 job-end
detection and the first-transfer exclusion; (3) stack-reset
interposition - a generation check per hop; a one-way guest
stack reset invalidates all lifted host frames, so a direct
chain must never span a reset (hint table of known resetters);
(4) interrupt interposition between transfers; (5) halts longjmp
out of callee bodies, which is why chained transfers must be
musttail - a growing host stack would corrupt the host_depth
accounting SPU_RET depends on; (6) diagnostic stepping
(SPU_LOCKSTEP, SPU_STEPTRACE, ...) observes every drain step and
must force the trampoline (spu_direct_init already disables the
fast path when any is set); (7) foreign/native seams enter and
leave with a clean trampoline - the job 00187280 override's exit
contract (ctx->pc = gpr[0], g_spu_trampoline_fn = 0); (8) ctx->pc
is set before the transfer, and image_id is restored across
image-crossing calls (SPU_DIRECT is intra-image).

## Prototype gates (scratch, ~/cc-callpath-work)

- Contract suite: the fork's own test_spu_direct passes in both
  modes with the designed signatures - with the fast path on,
  2,000,000 mutual branches take 1 drain step (vs 3,999,999
  off), hinted targets still drain every time, LS-0 still ends
  the job, the synthetic interrupt lands at iteration 1000,
  nested drain_ret_pc restores to 0x204.
- Eligibility: 98.96% of transfers take the fast path; every
  fallback in the sampled window targeted LS 0x00004, the
  hint-excluded site, by design.
- A/B (profile counters, per-dispatch): Battle 33,560 ->
  32,282 cycles (-3.8%); Campaign 32,837 -> 31,137 (-5.2%).
  The same transfer tax comes off every SPU job, not just
  00188300.
- Byte-equivalence, honestly: the driven byte-sum gate could
  not certify - the control pair (identical binary, fast path
  off twice) also diverges completely, because driven input is
  wall-clock-windowed (PAD_STICK seconds, PAD_FILE sleep jitter)
  and combat amplifies frame-level shifts. The machinery itself
  is frame-deterministic (two Campaign runs produced identical
  dispatch counts, 1,802,241), and the fast-path run sat inside
  the off/off spread on every sampled observable.

## Landing

Test build: build-fastpath + run-fastpath.sh, fast path on by
default; paired boot logs prove state (default: no "direct tail
calls off" line; PS3_SPU_DIRECT=0 control prints it). Jerry
play-tested on the real GPU: "definitely faster" (2026-11-09)
and directed the landing on master. Fork master 0b54109f (one
file, +7 lines); this repo: submodule bump + this note +
RUNTIME_PATCHES #32. Runtime opt-out: PS3_SPU_DIRECT=0.
