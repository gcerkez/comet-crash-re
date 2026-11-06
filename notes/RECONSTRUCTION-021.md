# Reconstruction notes 021 — camera/HUD extraction pass 1:
what the neighborhood actually holds

Date: 2026-11-06. Goal: replace the milestone-14 camera and
HUD estimates with values from the code. Baseline verified
first: repo tip 09c6ca1, clean tree. Method: call-graph and
body reads of the arenaGraphics neighborhood (0xFE000-
0x102000) plus the known render-path functions, then a dump
of the TOC data block the view code dereferences.

## Identities established (by call graph)

- FUN_000d8278 (game_loop's render-path caller) is the
  present/resource layer: it calls the 0x78Bxx framebuffer
  functions (incl. the FUN_00078b3c flip trigger) and an
  import thunk. It does not draw the scene.
- FUN_00101800 (836 lines, the neighborhood's largest) is a
  data/string shuffler — 65 calls to the memcpy-class helper
  FUN_000b8074, 8 to a string helper. Not graphics.
- FUN_000ff9d8 is per-entity projection math: VMX vector
  permutes/subtracts over per-entity tables (DAT_005520c0+
  indexed by entity), calling the 0x133xxx math family
  (FUN_00133f54, FUN_00133920 — the transform chain). The
  camera lives upstream of this, in whatever fills the
  view state it reads.
- FUN_000ff6d0: small (89 lines), consistent with the
  flip-trigger caller found in milestone 13/18.

## The view-state block (0x1F5D90-0x1F5F00)

The TOC block the graphics code dereferences is a mixed
pointer/constant table: the op_dispatch table pointer
(0x1F5D94 -> 0xF6A28), the arenaGraphics assertion string
pointers (0x1F5E74/78/84/90/94/9C), pointers into a
pi-family math constant table at 0x17C520+ (1.0, 0.0, NaN,
0.5, 2/pi, pi/2, ...), and inline floats 0.05, 0.03, 0.5,
1.0, -1.0, -pi/2, 2000.0, plus 9728.0/10496.0/9729.0/
24601.0 (page-sized values — heap/arena parameters, not
camera). NO clean camera set (eye/tilt/fov) sits in this
block: the projection is computed per frame in the 0x133xxx
VMX transform chain from state filled elsewhere. The
milestone-14 estimates therefore STAY labelled estimates;
nothing was tuned by eye in this pass.

## Mission flow (from the handlers)

- FUN_000fa2f8 (level-select path): stores the mission id
  into per-level state arrays (DAT_00551ebc/ec0, indexed by
  level), calls level_load_file, then updates a per-level
  best value: a ratio of two per-level floats (551e70 /
  551e6c) scaled by data constants, clamped against the
  stored best at 551e68, flag set at 551e84, counters reset
  (state field set to 0xd). This is the mission
  best-time/rating bookkeeping around a level load.
- Handler FUN_000f7a68 (a level-start op): seeds per-level
  state from the level table (DAT_005520a0 + 0xc), calls
  level_load_file, then works on the per-level runtime
  object at level_ctx + 0x53654C via FUN_00134fb0 /
  FUN_001339d0, with an allocation/lookup call
  FUN_000b1d80(300) and FUN_00142f0c(..., 0x16, 1).
- op_dispatch is hierarchical: handler FUN_000fbdf8 itself
  dispatches sub-ops; FUN_000e1e58 (675 lines, no string
  refs) is the standalone op caller beside game_loop —
  the command processor.

## Next probes for the camera (in order)

1. The 0x133xxx transform family (FUN_00133920's five-double
   signature first) — what matrices it builds and from what
   state.
2. The arena-init op handlers: which one writes the view
   state ff9d8 reads (trace writers of the 0x1F5E60-0x1F5E98
   fields and of DAT_005520c0 tables).
3. FUN_00100218 (calls FUN_0014d188 x4) — unread this pass.
