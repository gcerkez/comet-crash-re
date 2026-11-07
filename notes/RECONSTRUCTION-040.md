# RECONSTRUCTION-040 — the MultiStream MP3 codec ABI (for a native host replacement)

Spec 2026-11-07, from the lifted MultiStream SPU image
(spu_3f69ca9e…, image 2), the guest loader in the PPU corpus, and
the SPU runtime source. Purpose: implement the codec on the host
(minimp3) at the runtime's PIC stub seam — no Sony firmware
(notes 039 addendum).

## The seam

runtime/spu/spu_channels.c, `spu_indirect_branch`: a branch to an
address with no lifted function whose target word is 0 (zeroed LS),
with 0x1000 ≤ pc < 0x3E000 inside a live call frame, is
stub-returned: r3 = 0, SPU_RET. That is exactly where codec calls
land today. The neighbouring SPU_INTERP_UNLIFTED fallback (the
You Don't Know Jack FMOD precedent) proves this dispatch point can
hand a target to non-lifted code and rejoin.

## Call sites — exactly two, one entry point

Both call through the single function-pointer slot at LS 0x3C4B0,
whose value is 0x1A900 (module base = entry offset +0), written
once by the load arithmetic in spu_func_00006424:

- Site A: spu_func_00006D48 (spu_recomp.c:4609), lr=0x6E2C.
- Site B: spu_func_00006E68 (spu_recomp.c:4646), lr=0x6EB0 — called
  conditionally from …_00006D48 first, selected by fields at LS
  0x36F00/0x36F10.

The image has 10 indirect call sites; the other 7 draw targets
from internal tables, never the codec slot. (The stub log caps at
8 prints total, so the host hook keys on the target RANGE, not on
observed sites.) A separate install path (spu_func_00008A10) passes
r3=0x1A900 as data to a copy/relocate routine via a different slot
(0x3C580) after zeroing LS [0x1C900, 0x1E100) — it never enters the
codec and does not need to succeed for the hook to work.

All MFC DMA is caller-side (spu_func_00006F48, 4096-byte chunks,
progress at LS 0x1A690): **the codec is pure LS compute** — it
reads MP3 from LS, writes PCM to LS. That is what makes a host
substitution clean.

## ABI (word 0 of each register)

Calling loop: setup spu_func_00006CB0 — per-stream workspace
stride 4992 (0x1380), workspace LS base 0x3C280; an inner loop
(spu_func_00006D08) makes up to 3 sub-calls per stream pass.

- r3: stream input cursor = base + bytes consumed so far this
  pass (advances exactly by the codec-reported consumed count).
- r4: parameter block (task stack sp+32).
- r5: param block + 16 (sub-struct pointer).
- r6: 0x2C900 — fixed LS work buffer #1.
- r7: stream base + 2176 — per-stream workspace.
- r8: stream base + 4992 + 2176 — per-stream workspace.
- r9: 0x34100 — fixed LS work buffer #2.
- r10: site A: stream base + 4992; site B: 0x35480.
- r11: site A: r10 − 512; site B: stream base + 4480.
- r12: 0x37018 — fixed LS config struct.
- r13: 0.

There are no separate init/decode/close entries; operations are
discriminated inside the param block / stream context.

Return protocol (identical at both sites):
- r3 == −1 → error dump path (spu_func_000072DC).
- r3 == 0 → spu_func_00007290.
- r3 other → swizzled into a stream-context field (+14 halfword) —
  a codec-reported per-stream quantity (produced samples /
  position; exact semantics open).
- Param block +0x20 (word): written BY THE CODEC = input bytes
  consumed; the caller adds it to its cursor in every path.
- Param block +0x30 (word): codec-set flag; if −1 after the call,
  the caller clears it and proceeds to output DMA
  (spu_func_00006EF8) — reads as an end-of-stream/flush signal.

## Instances and state

One MultiStream task; calls are strictly serialized; work buffers
sit at fixed LS addresses → no concurrent codec instances.
Per-stream identity rides the workspace pointers (r7 is the stable
key candidate; r4 is a stack address, identical across calls).
MP3 decode is stateful (bit reservoir) → the host keeps one
minimp3 state per stream key. Host state must NOT live in LS (the
guest zeroes parts of the region and owns the footprint).

## Registration expectations (PPU side)

FUN_000a3ba4 reads the whole file (size from lseek-END; only a
0-byte read errors) and FUN_000a9564 merely extracts an ELF32
descriptor (EM_SPU check; entry from e_entry; span from section
headers). Nothing downstream verifies the load succeeded before
the SPU task starts calling the entry — so the host hook does not
need to fabricate a file or an ELF; the loader can keep failing.

## Open questions (for the probe)

Two things statics could not pin: (1) r3's exact out-semantics in
the non-zero path; (2) the PCM layout (width/order) the codec
leaves in the r6/r9 work buffers — float conversion happens in
MultiStream outside the codec, so integer PCM is expected. A
logging probe at the seam (PS3_CODEC_PROBE: dump r3–r13, the
param block, and the input head — MP3 frame sync should be
visible at the r3 cursor) settles the input side and the param
block's operation discriminators from real calls; the output side
gets settled by implementing decode and watching the cellAudio
ring go non-zero.
