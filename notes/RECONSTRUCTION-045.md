# RECONSTRUCTION-045 — Audio: the match-entry MP3 decode wedge

## Symptom

Jerry, 2026-11-07 ~20:48 EST: the recomp's audio is "tinny" and
"bubbly", and sounds overwhelm when several play at once. The
reinvestigation found a harder defect underneath the quality
complaints: in driven sessions ALL audio stops ~50–60 s in — right
at match entry — and never returns.

## Method

Temporary env-gated (`PS3_CODEC_TRACE=1`) per-call tracing inside
`ps3_spu_mp3_pic_invoke`, plus a one-shot "wedge dump" after 200
consecutive zero-consume calls at one cursor (cursor bytes, param
words, and the decoder's raw `samples`/`frame_bytes` outcome).
Offline micro-harnesses replayed the fork's vendored minimp3
against the extracted assets. Diagnostics were reverted before the
fix commit; the fork carries only the fix.

## The caller protocol (ground truth)

MultiStream's SPU task is **frame-synchronous**:

- One stream workspace for this title (r7 = 0x34980 the whole
  session; the 8-entry host table never sees a second key).
- It stages **one MP3 frame** at the staging-ring base
  (LS 0x32900) and calls the codec; site B (lr 0x6EB0) is the
  decode call, site A (lr 0x6E2C) primes. The caller's byte cursor
  (gpr98) advances by the codec's reported `consumed`
  (param+0x00); the PCM byte count goes in param+0x10 and gates
  the output DMA. `consumed == 0` means the caller re-offers the
  same staged frame forever.
- Healthy menu flow: ~104-byte music frames and larger SFX frames,
  one per call, cursors advancing within multi-frame chunks when
  staged together.

## The wedge (proven)

- Pre-fix trace: 206,152 calls, of which 182,515 are consecutive
  site-B calls at a frozen cursor 0x32900 with sync present,
  hold=0, consumed=0. The mix capture confirms silence from ~60 s.
- The staged bytes at the wedge are **byte-identical** to the
  asset files (verified by needle match and a full 522-byte diff,
  zero differences). All 60 extracted MP3s decode cleanly
  standalone; minimp3 is frame-exact vs ffmpeg on the soundtrack.
- A fresh minimp3 cannot decode the wedged frame in isolation
  either: mid-stream frames reference bit-reservoir data from
  their predecessors (`main_data_begin`), the reservoir lives in
  the one persistent decoder state, and **once any single decode
  fails, every later frame fails with it** — while `consumed=0`
  freezes the caller's staging at that frame. Permanent silence
  from one bad frame.
- Wedges strike at stream transitions (the 4th frame of freshly
  started loop streams, twice, in two different loop files):
  interleaved streams sharing one decoder state (music ↔ SFX ↔
  loops, reset only on a format change) corrupt each other's
  reservoir accounting. Frames mis-decoded around the same
  boundaries are the "bubbly" artifact; a straddle misparse that
  reports a longer frame than the header allows (823 B for a
  522 B frame, reproduced offline) desyncs the caller's cursor
  accounting the same way.

## Fix (fork, `runtime/spu/spu_mp3_codec.c`, "CODEC FIX2")

The seam now behaves like the frame-synchronous codec the caller
expects:

1. **Validate before consuming.** A decode is accepted only when
   minimp3's reported byte count equals the frame length computed
   from the header at the cursor (`mp3_frame_len_at`). A misparse
   over stale staged bytes is never consumed.
2. **Format change = new stream.** A rate/channels change resets
   the decoder state and the tail.
3. **Whole-frame tail + reservoir rebuild.** The seam keeps the
   most recent consumed frames (up to 4 KB, whole frames only).
   On a failed decode it replays the tail through a fresh decoder
   — rebuilding the bit reservoir exactly — then decodes the
   staged frame and adopts that state on success.
4. **Skip-with-silence as the last resort.** If the frame still
   cannot decode, the seam consumes its header length and emits
   one frame of silence. The caller's staging always advances, so
   no stream can wedge again; the skipped frame joins the tail so
   the next frame can heal via step 3.

## Verification (same drive that wedged pre-fix)

- Codec calls: 206k spin → **9,399 total, 9,311 consuming
  (99.1%)**, consumption uniform across all session deciles, the
  final call of the session still consuming.
- Path census (temporary instrumentation): 9,243 direct decodes,
  30 tail-rebuild recoveries, 18 skips — all recoveries/skips in
  the menu-phase transitions, none in battle.
- Battle phase decodes a mono loop stream continuously with PCM
  peak ~15.7k/32k — real audio at healthy level.

## Still open (deliberately not in this change)

- In the headless driven session the mixed output still goes
  silent after match entry although the codec produces good PCM —
  the loss is downstream of the codec (caller DMA / port / mixer
  state in that setup). Jerry's real sessions do have gameplay
  sound. A guest-memory delivery probe was inconclusive.
- The 44.1 kHz assets vs the 48 kHz output chain: whether the
  seam owes the caller a resample (the "tinny" suspect) is still
  unmeasured — the discriminating test is an isolated SFX capture
  with music off, compared against the asset's spectral peak.
- The cellAudio mixer hard-clips the summed ports to [−1,1] with
  no normalization — the standing suspect for "sounds overwhelm
  when they stack" (RPCS3's normalize-on-downmix is the
  precedent). Separate change, A/B'd on its own.
- Jerry (2026-11-07 22:09 EST) is open to integrating the
  firmware's real `flashMP3.pic` decoder instead, provided a
  fully built image stays distributable to his own machines;
  that trades away the freely-distributable goal and is his
  decision after the native path is measured.
