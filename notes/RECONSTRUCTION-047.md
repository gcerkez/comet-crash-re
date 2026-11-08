# RECONSTRUCTION-047 — Audio quality: the pitch is right; resampling exonerated

## Question

Jerry (2026-11-07): with music off, the sound effects alone still
sound "tinny", "bubbly", "not what they should be", and stacked
sounds overwhelm. All 44 base-game MP3 assets are 44,100 Hz while
the output chain runs at 48 kHz, so a missing or wrong
44.1 → 48 kHz resample was the lead suspect for the wrongness:
it would shift every pitch up by 48000/44100 = ×1.0884.

## Test

The driven profile already runs Music 0% / Effects 40%, so a
driven session's AUDIO_WAV capture (the final 48 kHz mix — what
the player hears) contains isolated menu SFX with no music. The
loudest menu transient in the 30–60 s window of the 2026-11-08
sanity capture was windowed and FFT'd.

## Result

Dominant peak: **5179 Hz**. The `item_select_change` asset's own
spectral cluster is **~5176 Hz**. Agreement within one FFT bin
(~12 Hz). Under the missing-resample hypothesis the peak would
sit at ~5634 Hz; it does not. (The other menu candidate, BEEP4 at
~5647 Hz, would appear at ~6146 Hz shifted; also absent.)

**The output pitch is correct. Sample-rate conversion somewhere
in the chain (host codec / mixer / SDL) is doing its job; the
tinny/bubbly quality complaint is not a rate error.**

## What remains suspect for the residual quality report

1. **Mixer hard-clip.** The cellAudio mixer sums ports and clips
   to [−1, 1] with no normalization or headroom management — that
   fits "sounds overwhelm when several execute in a row or more
   than one at a time" precisely. RPCS3's normalize-on-downmix is
   the reference alternative. This is now the lead suspect.
2. **Codec seam stream-state reuse.** The host codec keeps one
   decoder state per stream workspace and never resets it at a
   stream switch; a new sound on a reused workspace starts from
   the previous sound's leftover state. Unproven; would colour
   individual SFX onsets rather than pitch.
3. The firmware-decoder route (Jerry's 2026-11-07 proposal)
   remains open as a decision, pending these native results.

The decoder itself was already exonerated separately: minimp3
and ffmpeg produce identical decodes of the soundtrack and SFX
corpora (frame counts, durations, zero lost syncs).
