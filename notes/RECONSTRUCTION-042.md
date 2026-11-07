# RECONSTRUCTION-042 — Rendering performance: the NV3089 AA blit fallback

Date: 2026-11-07. Fork commit 9ec0d82c (local). RUNTIME_PATCHES #16.

## Baseline and a measurement caveat

All earlier headless numbers (17–20 fps) were measured under Xvfb, where
Mesa reports "No DRI3 support detected" and the Vulkan backend falls back
to llvmpipe — software rendering. Those numbers say nothing about the GPU
path. Real-GPU measurement: launch on the desktop display (RADV, RX 7900
XTX), drive Campaign to the Kreios title card, count RSX_PRESENT frames
over an exact 60 s window. Stock build: **37.0 fps**, GPU busy 23–29%,
one FIFO thread at ~82% CPU (runnable, not blocked).

## Where the time went

Stack sampling (eu-stack) of the FIFO thread on the real-GPU session:
`vk_eng_readback`, `rsx_draw_engine_guest_read`, `nv3089_blit`,
`sink_end`, `rsx_texture_decode`, libc memcpy. An NV3089 gate log
(NV3089_GATE) censused the blits: 6,109 of 6,388 in a minute of play are
one shape — **out 512×512, in 1024×1024, ds=dt=2.0, source pitch 10240**
(the sample grid of the 1280×720 AA scene surface), destination a 512×512
patch of a 1280-pitch main-memory texture. These are the game's
scene→effect-texture copies (the water/effects sampling source; the
toolkit's own comment in cellGcmSys.c calls the analogue "the single most
sampled blended texture in the frame").

`rsx_draw_engine_blit` declined every AA-source blit, so each copy took
the guest-memory path: a full 1024×1024 readback with a pipeline drain
(`submit_and_wait`), a CPU nearest-neighbour downscale, and — when the
destination texture was next sampled — a re-decode and re-upload. ~3× per
frame. The GPU was never the problem; the frame was serialized through
guest memory.

## The fix (fork 9ec0d82c, rsx_draw_engine.c only)

The handler already converts ds/dx into a source rectangle in source
texels, so the engine's rectangle was always in sample units. Three
changes, all inside `rsx_draw_engine_blit`:

1. Drop the source-side AA decline (destination-side AA still declines).
2. Clamp the source rectangle against the **sample extent**
   (`w*grid × h*grid`) instead of the logical dims — the old unit mix is
   also what cropped the full-frame blit in the notes-035 incident.
3. AA sources sample **nearest**, not the caller's "scaled means linear"
   heuristic — nearest is the fallback path's semantics.

Non-AA blits are byte-identical to before.

## A failed first attempt (kept for the record)

v1 scaled the rectangle by the grid and forced LINEAR. Screenshot test:
turrets visibly desaturated and framing shifted. Wrong on all three
counts — the rectangle was already in texels (so scaling double-scaled),
the clamp divided where it should multiply, and LINEAR averages samples
the CPU path never averaged. Reverted before it ever landed anywhere.

## Verification

- Menu frame byte-identical (md5) before/after.
- Real-GPU gameplay and battle-end screens visually correct (desktop
  captures): full board, complete HUD text, no crop, no wash-out.
- Same 60 s protocol after the fix: **71.0 fps** (from 37.0).
- Post-fix stack samples: `vk_eng_readback` and `rsx_texture_decode` no
  longer appear; the FIFO thread's remaining time is draw dispatch
  (`sink_end`).
- The intermittent GUI-atlas strip (Jerry's 2026-11-07 report) was seen
  once during the v2 gameplay capture and was gone by the next capture;
  it is documented on the stock build from earlier the same day, so it
  is pre-existing and not caused by this change. Root cause still open.

## Second-analyst cross-check (Gemini Flash, PERF_RESEARCH.md)

- Independently confirmed the AA blit fallback as the intra-frame flush /
  stall trigger (its Q2 Candidate A).
- Its addendum endorsed v1's scaling+LINEAR design as "correct" — that
  endorsement is **rejected**: v1 failed the screenshot test empirically.
  (The brief's addendum described the v1 approach; the analyst reviewed
  the description, not the measured result.)
- Its coherence verdicts for the blit bookkeeping (guest_dirty /
  color_serial bumps at the shared call site) check out against the
  source and apply to v2 unchanged.
- Its ranked next levers, adopted as the queue: per-draw descriptor set
  churn and per-draw render-pass cycling in vk_eng_draw; present-path
  fence waits; deferred batch fence waits (highest risk, last).

## Still open

Heavy-scene (many-unit) real-play retest is Jerry's; the 60 s protocol
above measures the title-card/early-game scene class. Next lever when
pursued: profile `sink_end` → `vk_eng_draw` per-draw recording costs
under a heavy battle.
