# Reconstruction notes 027 — hud_drawFrame read directly;
composer axis question narrowed

Date: 2026-11-06. Follow-up reads to notes 026.

## hud_drawFrame (FUN_000fed30) — the actual structure

Gemini's "layout table" framing needs one correction:
the gauge positions are COMPUTED per frame, not fixed
rectangles. The function (76 decompiled lines):

- Guards on two enable flags (slots -> 0x1F5E18, 0x1F5F9C),
  builds a display-scale vector by permuting state, and
  opens the HUD container at ctx+0x53646C via
  FUN_00133d18. FUN_00134e28(container) flushes/draws it.
- SPLIT mode (flag at slot -> 0x1F5FA8 == 0): TWO gauge
  widgets (ctx+0x5364CC and ctx+0x53650C, 0x40 apart).
  Each gets: a frame sprite via FUN_00135170(widget,
  0x6BF + small index), then anchor fills via
  FUN_00133934 / FUN_00133920 (the struct-filler family):
    x = ratio * (±0.7) * *(ctx+0x5364FC) + anchor
        (anchors at ctx+0x5364D0 / ctx+0x536510;
         -0.7 from 0x1F5F64, +0.7 from 0x1F5F68)
    y = *(ctx+0x5364D4) / *(ctx+0x536514)
    ratio (dVar10) = *(0x1F5F28)=0.05 / *(ctx+0x536500)
  Widgets attach via FUN_0014f330(container, widget).
- SOLO mode (flag != 0): ONE widget (ctx+0x53648C),
  frame sprite 0x6F8, same filler pattern with its own
  anchor/scale slots. THIS is the single-player HUD:
  one sprite-framed Thorium gauge, no numeric readout.
- The 0.4 / 0.65 constants (0x1F5F60 / 0x1F5F54) and the
  384.0 baseline (0x1F5F80) are consumed by the widget
  renderers (hud_playerStatusBar / hud_meterBar and the
  133xxx fillers), not by hud_drawFrame itself.

Port consequence: the original's in-arena HUD is a
sprite gauge system in a normalized space; a faithful
port needs the sprite atlas behind ids 0x6BF/0x6F8 and
the anchor state at ctx+0x5364xx decoded. The port's
panel HUD stays a labelled estimate until then — but
the solo-mode fact is already actionable design truth:
single player = one gauge.

## camera_composeMatrices — axis question narrowed

The composer's tail (VMX) splats the depth bounds
(-100.0 from 0x1F6098, 400.0 from 0x1F6094) into the
matrix rows and builds ±bound pairs from the camera
struct — an orthographic matrix built from frustum
bounds, stored at the runtime buffers. Its decompile
TRUNCATES at the matrix store ("Bad instruction"), so
which struct field (+0x2C plain vs +0x30 trig-scaled)
becomes the vertical bound cannot be settled from the
decompiled C. Settling it needs a raw-disassembly trace
of the composer's stores at 0x102D40+. The port keeps
tilt = 50 deg as elevation meanwhile (notes 026).
