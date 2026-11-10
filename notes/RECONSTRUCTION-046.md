# RECONSTRUCTION-046 — Pause menus step too fast: the pad report cadence

## Symptom

Jerry (2026-11-07, twice): in the in-game pause menu, the dpad and
arrow keys jump past the intended option — he has to backtrack —
while the main menu and pre-game menus navigate at a valid pace.

## Reproduction and quantification

Driven reproduction (Xvfb, PAD_FILE): a count-8 pad hold moves
exactly one step in the front-end menus but 2–3 steps in the pause
menu (three DOWNs overshot Settings and landed on End Game).

Ladder test in the Cheats submenu (identical menu code reached from
Options and from pause → Settings): from the top row, the second
step lands at a hold of ~14 reports and the third at ~22 in the
front-end; in pause the second lands at ~5 and the third at ~7.
The pause menu's held-button repeat clock runs ≈4× faster per
report than the front-end's.

Presented-frame rate is not the difference: RSX_PRESENT frame-field
deltas on the real GPU measured ~21 fps in the main menu, in a
match, and paused alike.

## Where the input timing actually flows

An HLE probe (temporary, since reverted) counted cellPadGetData
polls on port 0 per guest state on the real GPU: state 11
(loading) ≈ 40 polls/s, state 13 (match) ≈ 140/s, state 14
(pause) ≈ 280/s. A second probe counted frame_input passes:
~0.2/s in the gameplay states — frame_input is not the input path
that matters here. The pad layer is: the guest polls the pad
directly from its state handlers, at whatever rate its loop runs.

The fork's pad backend (libs/input/cellPad.c, pad_poll_backend)
served a FRESH report — len > 0, the packet titles drain on — at
most every PAD_POLL_INTERVAL_MS = 4 ms, i.e. 250 Hz. Any guest
timing that counts fresh reports therefore runs at up to 250 Hz
in a state that polls fast enough to consume them, and at the
state's own slower rate elsewhere. Menu auto-repeat is such a
timing. Pause polls at ~280/s → repeat runs at the 250 Hz cap;
front-end states poll slower → repeat feels normal. The measured
≈4× matches the cadence ratio 250/60 ≈ 4.2. On hardware a fresh
report arrives once per vblank in every state, so the defect
cannot appear.

## Fix (fork 1c4bd6b2, local at time of writing)

pad_poll_backend now serves fresh reports at a vblank-like 16 ms
default cadence. The PAD_POLL_INTERVAL_MS env var overrides
(4 restores the inherited behaviour). The change is in the poll
gate only; report content, the freshness/drain semantics, and the
PAD_FILE contract are unchanged.

## Verification status

- Probes reverted; the shipped tree carries only this fix.
- The Xvfb ladder cannot discriminate the fix: under llvmpipe the
  pause loop is poll-bound (it polls slower than even 62.5/s), so
  the cadence gate never engages there — the ladder reads
  identically before and after, as the model predicts.
- A real-GPU A/B (env 4 vs 16, count-8 holds in the pause menu)
  was attempted but the desktop's lock screen engaged mid-run and
  the captures are unusable.
- **Decisive check outstanding: Jerry's hands.** The built binary
  defaults to the 16 ms cadence. If pause navigation feels native
  and nothing else regresses (unit-send hold, front-end menus),
  the fix stands; the env override isolates it if not.

## Found along the way

The lift's state-0 handler (func_000E41CC) contains a foreign
debug hook: after 1300 state-0 calls it force-calls
frame_input(state=13) ("FORCE_STATE_13" env, "[STATE0_HOOK]"
printf). Not game code, not one of this project's patches — a
leftover from an earlier automated session. It sits in the boot
path and is unrelated to this defect. Removing it (and folding
the removal into the lift patch scripts so a fresh lift does not
carry it) is queued as its own change.

## Addendum (2026-11-08): the hook is out

Verified before removal: a fresh lift produced by setup.py does
not contain the hook, and neither does the lifter — it existed
only in this workspace's generated lift file (and the binary
built from it). Removed with patch_remove_state0_hook.py
(assert-anchored; the genuine body of func_000E41CC follows the
hook block and is untouched), rebuilt, and boot-verified. No
repo patch script needs to carry the removal: a fresh lift never
had it. The working lift's other patches (cheats Edits 1–4 + 6,
redraw, unlink) were re-grepped after the edit and are intact.
