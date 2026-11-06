# Reconstruction notes 031 — two more resources evaluated:
ps3-re (nothing) and ps3recomp (a real second route)

Date: 2026-11-06. Both suggested by Jerry; evaluated by
delegated readers, load-bearing claims verified locally.

## sleepthegod/ps3-re — skip

A hype README around one bash script: downloads a
firmware PUP, unpacks it, runs stock objdump over
STILL-ENCRYPTED SELF payloads (no decryption step
exists in the script), and greps the output for
name-shaped strings as its "databases". Its "critical
findings" are hardcoded echo strings; some named
firmware modules are not real SPRXs. Zero syscall /
signal / PRX mechanics documentation. Nothing to pull.

## sp00nznet/ps3recomp — a genuine second route

Mature static-recompilation toolkit (MIT, ~2,600
commits, v0.19.0): decrypted ELF -> PPU disassembly ->
lifted C++ -> linked against a clean-room HLE runtime
(LV2 syscalls, PRX libraries, RSX translation) -> native
executable. Claims the full PPC64 surface (integer, FP,
VMX, jump tables, lwarx/stwcx) — categorically beyond
AnyPS3, which stalled on this binary at 68/851 entries.
Has a real SPU story (disassembler/lifter + embedded
image extraction; this title carries two SPU images).
Proven on PSN titles: The Simpsons Arcade Game
(NPUB30563, same class as NPUB30061) is playable.

PROVENANCE (verified locally): Stylish-Dark's
config/ps3recomp.lock pins THIS repo at d3ed1a5c — it
is the upstream their recomp track consumed, and their
repo now keeps it as a behavioural oracle while their
hand-written port is the target. Same shape available
to us.

PROBE (run locally, ~/tools/ps3recomp cloned):
ppu_loader.py processed our EBOOT.ELF cleanly on the
first attempt — 851 functions (the OPD entry set,
matching AnyPS3's independent count) and an import map
identical to ours (20 libraries / 195 functions). The
lifter stage needs its documented analysis/stub
pipeline (generate_stubs.py, gen_hle_nids.py, project
template) — that IS the bring-up work, not a probe step.

Caveats: in-tree, only Windows/D3D12 is claimed to run a
recompiled game (Linux/Vulkan runs games in downstream
ports so far); per-title bring-up took others weeks to
months; our ELF is the US 1.10 patch (theirs EU 1.00),
so Stylish-Dark configs don't transfer verbatim;
cellSpurs status is ambiguous in their docs (prose says
Partial, table says complete; MODULE_STATUS.md is the
tie-breaker). Statically linked PSGL should lift as
ordinary PPU code over HLE cellGcmSys (inference).

DECISION PENDING (Jerry): authorize a time-boxed
bring-up attempt in parallel with the corpus/hand-port
line, or keep ps3recomp as reference only.
