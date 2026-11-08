# RECONSTRUCTION-048 — Sprite-heavy slowdowns: the per-draw shader walks

## Symptom and live profile

Jerry, mid-game (2026-11-08 ~01:10 EST): the game runs slow in a
sprite-heavy scene. Observed read-only (his session untouched):
the process burned ~380% CPU while the GPU sat at 5–38% busy —
CPU-bound, not GPU-bound. Thread stacks: the RSX FIFO thread
(frame_clock → cellGcm_rsx_process_fifo → sink_end →
eng_pipeline_get → rsx_fp_texture_expand_units) was the top
consumer; six SPU workers ran ~50% each on guest DMA work
(mfc_do_transfer / spu_coh_is_reserved).

## Root cause

eng_pipeline_get runs on EVERY draw to find-or-create the Vulkan
pipeline. Before its cache lookup it re-derived five quantities
by walking the fragment program's instruction stream end to end,
every time:

1. rsx_fp_texture_units — sampled texture units
2. rsx_fp_texture_gamma_units — gamma-corrected units
3. rsx_fp_texture_expand_units — sign-expansion units
4. rsx_fp_structural_hash — the program's pipeline identity hash
5. rsx_fp_collect_constants — the inline constant payload

All five are pure functions of the program bytes (2 and 3 also of
the 16 texture-derived input mask bytes). A sprite-heavy scene is
simply a draw-heavy scene: thousands of draws × five decode walks
over the same handful of resident programs.

## Fix (fork dc9878b2, FP MEMO PATCH)

A 16-entry memo in rsx_draw_engine.c keyed by an FNV-1a content
fingerprint of the program bytes (plus size, and the gamma/expand
input masks for those two results). One cheap byte pass replaces
the five walks. The fingerprint covers the bytes themselves, so
a guest program upload that rewrites a buffer can never hit a
stale entry; programs differing only in inline constant values
correctly miss the constants cache (their structural hash — and
therefore their pipeline — is still shared, as before).

Semantics preserved exactly: a zero structural hash (malformed
program) still returns 0; a failed constants collect still
returns 0; a program with more than 256 inline constants takes
the original direct-collect path. One internal change: the
pipeline key now mixes in the basis-seeded structural hash (the
same value eng_fp_debug_hash names a program by) as 8 bytes
instead of chaining the running key through the walk — key
values differ from before, key identity does not.

## Verification

- Temporary counters (FP_MEMO_STATS, stripped before commit):
  **327,680 pipeline lookups in one driven session, 236,836 memo
  hits (72%), 90,843 misses.** Every hit is five walks avoided.
- Rendering: driven Campaign match on the real GPU renders
  correctly — full board, correct textures/shading, HUD, effects,
  ship — in early and late captures. No missing or wrong shaders.
- Frame rate A/B under identical conditions (same drive, same
  scene, desktop locked): **19 fps pre-fix control vs 19–20 fps
  with the fix — no regression.** (The historical 72 fps figure
  was this protocol landing on the animated title card; tonight's
  drive sat in a static 96-draw scene on both builds, which is
  why the absolute numbers differ. The comparison, not the
  absolute, is the evidence.)
- Clean tree (diagnostics stripped) rebuilt and boot-to-match
  sanity driven: zero errors, audio envelope matches the
  known-good pattern.

## Still open

- 28% of lookups still miss (16 direct-mapped entries; programs
  × texture-mask variants thrash). A 64-entry or 2-way table is
  the cheap follow-up if a heavier scene shows it matters.
- The FIFO thread's next costs after this fix are the backend's
  per-draw work in vk_eng_draw (render-pass begin/end per draw,
  descriptor churn, the pre-draw barrier) — the GPU-offload
  direction Jerry endorsed 2026-11-08: fewer, bigger submissions
  instead of per-draw CPU ceremony. VK_DRAW_BATCH is already on.
- The SPU workers' DMA/coherence load is guest work; it scales
  with entity count and is the floor set by the game's own design.
- Jerry's heavy-scene session is the final verdict, as usual.
