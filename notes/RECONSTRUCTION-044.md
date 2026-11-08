# 044 — The right-side black band: backend blit guard dropped half the resolve tiles

## Symptom

In live matches (and battle-end/results screens) the frame's right
side, from x = 1536 to the right edge, was pure black at every
resolution and Thorium value, while menus and title cards were
correct. The board itself rendered whole in the scene surface; the
loss appeared between the scene and the screen. Reported from real
play after notes 042's speed fix landed ("it is sooooo fast! but
the right hand side is missing").

A first verification capture for notes 043 already contained the
band and it was misread as a dump artifact. Recorded as a process
lesson: an anomaly in a verification capture is a defect until
proven an artifact.

## Localization

Pass dumps (RSX_ENGINE_PASS_DUMP) of a mid-match frame:

- The scene pass renders the whole board (3840×2160 AA surface,
  content columns 19..3820).
- The display pass (4 draws into a 1920×1080 display buffer,
  sampling the resolve texture) writes content only at columns
  9..1535, with a hard edge (255 through ~1530, 0 from 1540).
- With RSX_GPU_BLIT=0 (CPU resolve fallback) the same display
  target is full width (9..1909). The fault is in the GPU resolve
  product, not the draws.

Eliminated in turn: the glue camera override, the guest
viewport/clip/scissor (all 1920×1080 in the VP log), the swapchain
and present fit (a left-aligned band cannot come from the centered
fit), the engine's surface record for the resolve texture
(created 1920×1080, verified by uncapped creation logging), the
engine's blit extent check (zero declines), and the draw-batch
path (VK_DRAW_BATCH=0 reproduces identically).

An env-gated diagnostic in the backend blit then printed the
ground truth: **5,592 dropped blits in one driven match, six
distinct rects, 932 occurrences each (one per frame)**:

```
src 3840x2160 dst 1920x1080  s(3073,1    768x1024) d(1536,0    384x512)
src 3840x2160 dst 1920x1080  s(3073,1025 768x1024) d(1536,512  384x512)
src 3840x2160 dst 1920x1080  s(3073,2049 768x112 ) d(1536,1024 384x56 )
src 3840x2160 dst 1920x1080  s(1,2049    1024x112) d(0,1024    512x56 )
src 3840x2160 dst 1920x1080  s(1025,2049 1024x112) d(512,1024  512x56 )
src 3840x2160 dst 1920x1080  s(2049,2049 1024x112) d(1024,1024 512x56 )
```

## Root cause

The guest resolves its AA scene into the 1920×1080 texture as
twelve NV3089 tiles (4 columns × 3 row bands, ds=dt=2.0) whose
source origins are (1,1)-based in sample space. The far column's
source therefore ends at 3073+768 = 3841 — one sample past the
3840-wide scene surface — and the far row's at 2049+112 = 2161,
one past 2160. `vk_eng_color_target_blit` guarded with a silent
return on any rect overshoot, so the whole right column and the
whole bottom row of tiles were discarded every frame. The engine
above it accepted the same blits (its own check uses the
destination rects, which fit exactly), so nothing logged anything.

On the RSX this read clips at the surface edge; the recomp's CPU
fallback clips too, which is why the CPU path rendered correctly.

## Fix (fork 4a97c240, RUNTIME_PATCHES #18)

In `vk_eng_color_target_blit`, replace the drop with edge
clipping: a source overshoot shrinks only the source extent (the
destination keeps its size; the edge texel stretches by less than
one sample); a destination overshoot shrinks both extents
proportionally to preserve the guest's scale. Blits starting
outside either surface are still dropped. The diagnostic prints
used to find this were removed again; the fork carries only the
fix.

## Verification

- Pass dump after the fix: display target content columns
  9..1909 (was 9..1535); bottom strip (rows 1030..1074) filled.
- Real GPU (desktop :0): the game window shows scene content to
  its right edge mid-title-card; no band.
- Frame rate (60 s RSX_PRESENT frame-counter protocol, in-match):
  72 fps — the entry-16/17 speed is retained.
