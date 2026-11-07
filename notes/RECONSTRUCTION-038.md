# RECONSTRUCTION-038 — the unit-send latch (input layer), and why it may never engage in the recomp

Full corpus investigation 2026-11-07 (all addresses in the 1.10
EBOOT; line cites are analysis/v2/decompiled.c).

## The mechanic, from the code

Sending is a **latched selection inside the per-player pad
interpreter** `FUN_0010d390` (0x10D390), fed by
frame_input → pads_update → pads_sample → FUN_0010c5b8 (raw→internal
conversion). It is not in the arena sim code.

- Conversion (FUN_0010c5b8): stick axes are read as u16, center 0x80:
  `v = (raw − 0x80) / 128`; if `|v| ≥ 0.2` (game deadzone) then
  `out = (v − sign·0.2) · 1.25`, else 0. Right stick → state+0xB0
  (X) / +0xB4 (Y) = padstruct+0x240/+0x244.
- Interpreter right-stick block, per frame:
  - mag ≤ 0.5: idle. mag < 0.1 clears padstruct+0x171 ("committed
    this tilt") but **deliberately leaves the lock alone** — that is
    the auto-send persistence.
  - mag > 0.5: menu engages; direction quantized against 8 compass
    vectors → slot 0–7 (padstruct+0x68, +0x2C). Slot→unit-type is the
    player's build state, validated by FUN_001637b8.
  - Fresh hard re-tilt (shaped mag crossing ~0.9 upward) clears the
    lock (padstruct+0x172 = 0) — re-entering the menu to change or
    abandon the selection.
  - **Commit:** with the menu armed, same slot held, pool valid, and
    **mag > 0.9**, a timer (padstruct+0x6C) accumulates dt; past
    **1.0 s** it fires action 0x0B once (FUN_00166de0) and sets the
    lock +0x172 = 1 and +0x171 = 1; padstruct+0x20 pins to 1.0.
  - While locked, each frame re-validates the slot's pool via
    FUN_001637b8; when it empties, +0x172 clears itself.
- What stops sending, exactly: a fresh hard re-tilt (then either a
  new ~1 s commit or none), or the pool running dry. Releasing the
  stick alone never stops it.
- Dropout rule in pads_sample: GetData error **or len == 0** replays
  the previous raw sample (input freezes, never zeroes).
- The game uses only cellPadGetInfo2 / SetPortSetting(port, 2 —
  press mode, self-enabled) / GetData / SetActDirect. No GetDataEx,
  no sensor reads in the pad path.

**Unresolved link:** the arena-side consumer that reads the lock
fields (+0x172/+0x68/+0x2C/+0x20) and physically launches units per
frame is not identifiable in the carved corpus (the frame_input
state handlers at 0xE2F30–0xE3260 are in the uncarved gap; the one
checked via the lift is a state-enter handler). Launch cadence is
therefore not yet known from code.

## The recomp divergence hypothesis (byte math, checkable)

The commit gate mag > 0.9 acts on the game's converted axes, so on
a pure-axis push it needs the raw stick byte ≥ ~246 of 255
(byte 245 → mag 0.893; 246 → 0.903). A DualShock 3 saturates: its
axes reach the byte extremes at the rim, corners included. The
recomp's SDL path (pad_sdl_axis_to_u8) maps SDL's −32768..32767
linearly with only a ±4500 deadzone and **no outer rescale**, so a
full mechanical tilt of an Xbox-style pad (circular gate, typical
rim readings ~28–31k per axis, ~23k per axis on a 45° diagonal)
lands at bytes ~218–245 — the menu engages (0.5 needs only ~205)
and directions highlight, but the 0.9 commit gate is marginal to
unreachable, the lock never sets, and auto-send never starts. The
XInput path rescales past its deadzone but has the same rim
shortfall. This matches the reported symptom while basic stick
response looks fine.

**Candidate fix (fork cellPad):** radial outer-edge boost for both
sticks — scale the stick vector so the mechanical rim reaches full
scale (gain ≈ 1.2, clamped at the byte extremes). The left stick is
length-clamped to 1.0 by the interpreter anyway, so ship movement
is unaffected. Confirm the symptom shape with Jerry first (menu
highlights but never commits; a really hard rim push held ~1 s may
occasionally kick it off), then patch pad_poll_sdl2's axis fill (and
the XInput fill for symmetry), rebuild, and have him retest with
the real controller.
