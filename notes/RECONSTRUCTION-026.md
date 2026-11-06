# Reconstruction notes 026 — Gemini Pro second opinion,
cross-checked claim by claim; the camera is solved;
a correction to notes 025

Date: 2026-11-06. The Antigravity route went live (LS +
ANTIGRAVITY_CSRF_TOKEN + ANTIGRAVITY_PROJECT_ID for the
comet-crash-re project 15f3e8ff). Gemini Pro conversation
04db587d (309 steps, read-only; repo verified untouched
afterwards) returned a full camera/transform/HUD/state
report. Every checkable anchor was verified against the
ELF and corpus before anything here was adopted.

## Verified (evidence in the ELF/corpus)

CAMERA — the system the port now uses (milestone 17):
- Tilt constant 0.872665 rad = 50 deg at TOC 0x1F6014,
  loaded by camera_computeView (FUN_00100aa8) and fed to
  a sincos helper (FUN_000b589c, mode 1).
- camera_composeMatrices (FUN_00102d40) calls
  camera_computeView with distance 18.0 (0x1F607C) in
  overview, 11.0 (0x1F6080) when the per-player follow
  flag (&DAT_00551f1c) is set — visible verbatim in the
  decompile (slots -0x6744 / -0x6740).
- Zoom: runtime value in DAT_005520d8, clamped [0.0, 0.2]
  (0x1F5CC4 / 0x1F5D5C), step 0.005 (0x1F5D58), in
  camera_zoomInput (FUN_000f3db0, body now extended to
  its true 2,696 B). Zoom scale = 1/(1-zoom), read
  directly in camera_computeView: F/(F - zoom), F=1.0.
- Default centre (12.0, 12.0) at 0x1F5FFC — world unit
  is the grid cell. Depth bounds -100.0 / 400.0
  (0x1F6098 / 0x1F6094).
- Matrices are composed on CPU into runtime buffers
  0x54DFA0 (projection) / 0x54DFE0 (modelview) and
  uploaded by scene_submitDraw (FUN_000fc1a0, carved
  this pass, 1,312 B) via psgl_loadMatrixf (FUN_00017de0,
  renamed; call visible in the corpus at the 0x54DFA0
  reference). game_loop calls scene_submitDraw at
  0xE4160 / 0xE45CC / 0xE466C (all three verified).
  This refines notes 024: the four psgl_ortho sites are
  the post/GUI paths; the 3D scene uploads composed
  matrices instead.

PIPELINE CORRECTION (Gemini right, notes 025 wrong):
- 0x106B58 has NO function prologue (first words are VMX
  ops) — it is the tail of FUN_00105d80. Ghidra fixed:
  false function removed, FUN_00105d80's body extended
  to 16,600 B and renamed asset_preloadModels. It is
  spawned from game_loop (call at 0xE5080, verified) via
  FUN_000dcf10 + a thread entry whose OPD (0x1F3F08)
  resolves to 0x109E58 (verified) — a background model
  preloader.
- obj_loadModel (FUN_00124e80, 31,272 B) is the Wavefront
  OBJ/material loader: the vocabulary strings exist in
  the ELF (usemtl 0x17E360, useTeamColor 0x17E400,
  lightMapColor 0x17E480, modelViewProj 0x17E458), it is
  called 50x by the preloader stepping the registry, and
  its STL-vector + constructor density fits a parser.
  (My "transform core" reading had no positive evidence;
  retracted.)

HUD — hud_drawFrame (FUN_000fed30, 768 B) is called by
game_loop at 0xE4638 (verified, plus 0xE41B8 / 0xE46B0).
Layout constants verified in the TOC block: Thorium
gauges at x = -0.7 / +0.7 (0x1F5F64 / 0x1F5F68), y = 0.4
(0x1F5F60), w = 0.05 (0x1F5F28), h = 0.65 (0x1F5F54);
baseline 384.0 (0x1F5F80). ff9d8's anchor pairs verified:
(71.0, 11.5), (71.0, 41.6), (12.0, 67.0) at 0x1F5FF0+.
Component renderers renamed from their verified format
strings: hud_textFormatted ("%d %s %s", also binds
gui_quad_shader), hud_textRatio ("%s (%d/%d)"),
hud_textCount ("%s %s %d" / "%s %d"); plus
hud_playerStatusBar, hud_meterBar, gui_submitQuad,
gui_emitGlyph, debug_fpsOverlay per the vtable/call
evidence.

STATE MACHINE (Gemini): current state in DAT_005520c0;
state 13 = arena gameplay; arena entry calls
FUN_000e5808 (renamed arena_enterInit), in whose
neighbourhood ff9d8(player, 1) is called (verified at
FUN_000e57c0 — the e57c0/e5808 boundary is fuzzy because
e5808's decompile has a pcode error). The state table
detail (states 7-14) is recorded as Gemini-sourced,
not independently re-derived.

## Not verified / left open

- Exact vtable-slot -> function mapping for two HUD
  components (OPD words exist at 0x1F2100-0x1F2370; the
  family is certain, two assignments are Gemini's).
- arena_enterInit's toolbar sprite-id lines (its
  decompile is truncated by a pcode error).
- Which of sin/cos(tilt) scales which camera axis in
  the composer — the port uses tilt as the view
  elevation (50 deg from the ground plane); if a later
  read of camera_composeMatrices shows the complement,
  the port's pitch flips to 40 deg elevation.
