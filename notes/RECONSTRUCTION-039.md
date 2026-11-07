# RECONSTRUCTION-039 — total silence: MultiStream has no MP3 codec (dev_flash missing)

Diagnosis 2026-11-07 (dedicated investigation; every claim from run
logs or code). Jerry's symptom: no sound at all in the recomp.

## The evidence

- cellAudio initializes perfectly in every log: Init → PortOpen(8ch,
  8 blocks) → mixing thread → PortStart. The SDL device opens with
  no error. Ports are running, so the mixer's "has data but never
  started" warning (cellAudio.c:451-463) never fires.
- With AUDIO_PEAK=1, the mixer's raw ring diagnostic (cellAudio.c:
  482-495 — raw guest floats, no byte-swap, no gain) printed an
  all-zero ring at every sample across a full boot-to-menu run
  while the read index advanced ~8,800 blocks. **The guest audio
  buffer is never written with a nonzero byte.** That single fact
  excludes every mixer-side hypothesis (gain, endianness, phase)
  and everything downstream (SDL device, speakers, restarts).
- All 25 run logs contain, exactly:
  `Multistream: mp3 failed to load MP3 codec (are you using the correct flash?)`
  and `[fs] open FAIL '/dev_flash/sys/external/flashMP3.pic' -> 'D/sys/external/flashMP3.pic'`.
  Plus 8 capped lines per run: `[spu] img=2 stub-return 0 from
  zeroed PIC buffer at LS 0x1A900` — runtime/spu/spu_channels.c
  (~2925) names this exact case in a comment: calls into the
  unloaded PIC plugin slot are stubbed to 0, so the MultiStream
  SPU task cycles forever, decoding nothing into silence.

## The chain

Every audio asset the game owns is MP3 (all music and SFX in
data1.pak/data2.pak — verified on the extracted members). Decoding
is done by Sony MultiStream, an SPU task, which does not embed its
MP3 decoder: at setup, the guest's codec loader FUN_000a3ba4
(decompiled.c:86872; string 0x182D10 via TOC slot 0x1F7CB4) opens
`/dev_flash/sys/external/flashMP3.pic` from the console's firmware
flash and registers it (FUN_000a9564). Its caller ignores the open
failure (decompiled.c:91061), so the game proceeds, muted by
construction.

The recomp side was never configured to serve /dev_flash:
ppu_fs.cpp:170-180 resolves it via PS3_DEV_FLASH, falling back to a
leftover Windows path (`D:/recomp/tools/rpcs3/dev_flash` → the
relative `D/...` in the log). No launcher set PS3_DEV_FLASH, and
the local RPCS3 tree's dev_flash dirs are empty (no firmware ever
installed; no PUP on disk before today).

## The fix (prepared 2026-11-07)

1. Downloaded the current official firmware (4.93) PUP from Sony's
   update CDN to ~/Downloads/PS3UPDAT_493.PUP (206,197,916 B; URL
   from Sony's own ps3-updatelist.txt). Modern PUPs ship dev_flash
   as encrypted SPKG packages, so hand-extraction is out; RPCS3's
   firmware installer is the tool. Headless attempts via the
   RPCS3 AppImage in ~/Downloads failed (its Qt xcb plugin will
   not initialize under bare Xvfb/SSH) — **Jerry does this one
   step in the GUI: open RPCS3 → Install Firmware → pick the
   downloaded PUP.** That populates ~/.config/rpcs3/dev_flash.
2. Both launchers now pass PS3_DEV_FLASH, defaulting to
   ~/.config/rpcs3/dev_flash (tools + repo run.sh).

## Watch item for the verification run

The PIC is SPU code loaded at runtime into LS 0x1A900, outside the
statically lifted image. If, with the codec present, the log shows
the task branching into unlifted LS space instead of the PIC
stub-returns disappearing into real decodes, the SPU runtime's
interpreter fallback for runtime-loaded code becomes the next
task. Confirmation of the fix itself: "mp3 failed" and the fs
open-FAIL lines gone, AUDIO_PEAK > 0, ring diagnostic showing
non-empty blocks — and music at the menu.

Side finding (not the cause, fix when next in the file):
cellAudioPortOpen drops the guest's CellAudioPortParam.level
(cellAudio.c:837-869); today the mixer's `level<=0 → 1.0` coercion
masks it.

## Addendum (same day): firmware route scrapped — native codec instead

Jerry's direction: no PlayStation firmware at all. His end goal
for this project is a fully native, self-contained, freely
distributable AppImage, and a Sony firmware file is neither native
nor distributable. The PS3_DEV_FLASH launcher wiring from the fix
above was removed the same day (the downloaded PUP remains in
~/Downloads for his RPCS3 use, untouched by the game).

Replacement plan: implement the codec's function on the host, at
the exact seam the SPU runtime already stubs (the PIC call sites
into LS 0x1A900, runtime/spu/spu_channels.c ~2925). When
MultiStream calls the codec, the host decodes the MP3 with
minimp3 (public domain, single header — already the decoder the
native hand port uses for the same assets) and returns PCM in the
codec's place. No Sony code, no firmware file, nothing that
changes the distributable status of the bundle. First step is a
spec of the PIC ABI from the lifted MultiStream image (entry
offsets, arguments, stream state, buffer formats).
