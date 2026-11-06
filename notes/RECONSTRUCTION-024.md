# Reconstruction notes 024 — the projection is ORTHOGRAPHIC;
there is no perspective camera (milestone 16)

Date: 2026-11-06. With PSGL named (notes 023), every
projection setup in the binary is enumerable. There are
exactly four psgl_ortho sites and no other projection
function is ever called:

- arena_setSceneView (FUN_000fc778) — arenaGraphics' view
  setup, called at the head of arena_postChain.
- arena_postChain itself — the post-pass box.
- arena_drawFullscreenQuad (FUN_000fc828) — final composite.
- gui_setView (FUN_00133eb0) — the GUI/menu view.

## The shared view box

Both the arena view and the GUI view resolve to the SAME
orthographic box (slot arithmetic from TOC 0x1FC7C0):

    glOrtho(left=0.0, right=*0x87303C, bottom=*(0x2BBFAC)=1.0,
            top=*0x873048, near=0.0, far=1.0)

- left/near are the float 0.0 stored at 0x1F5E20 (arena)
  and 0x1F6B28 (GUI); far is 1.0 (0x1F5E38 / 0x1F6B20).
- bottom dereferences 0x2BBFAC, a float 1.0 inside the
  display-mode table at 0x2BBF94+ (int pairs 720x480,
  720x480, 480, 256, ... — the cellResc mode table).
- right/top dereference two SHARED BSS floats at
  0x87303C / 0x873048 — the current display width/height
  in the game's screen units, written at mode init
  (runtime values, not file constants).

So menus, arena and HUD all draw in one screen-space
orthographic space; the "3D" arena look comes from
MODELVIEW transforms in the scene draw, not from a
perspective projection. That is why three passes of
hunting for perspective constants found only pointers.

## Port change (milestone 16)

native/src/main.cpp: project() is now orthographic —
uniform pixels-per-world-unit scale (c.focal re-derived at
the field centre so framing is unchanged there), depth
kept only for painter sorting. The eye/tilt modelview is
still the labelled estimate; the original's scene
modelview is the next extraction (the scene draw reached
from FUN_0012f66c). Verified: build clean, ctest 6/6,
headless level33 frame renders with HUD + font + placed
content (shot_ortho_level33).

Renames this pass: fc778 arena_setSceneView, fc828
arena_drawFullscreenQuad, 133eb0 gui_setView, fce34
arena_renderTargetFlag; corpus re-exported.
