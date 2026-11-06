# comet-crash-re

Reverse-engineering project: reconstructed code and logic from **Comet Crash**
(PS3, NPUB30061, Pelfast, 2009) — a tower-defense game whose main executable
was only available as an encrypted PS3 SELF (`EBOOT.BIN`). This repo holds the
analysis corpus and reconstruction notes produced from the decrypted binary.

**This is a personal research project for a game I own. It contains analysis
output (decompiled pseudocode, inventories, notes) — not the game's assets or
the game binary itself.**

## The target

| | |
|---|---|
| Title | Comet Crash |
| Title ID | NPUB30061 (US PSN) |
| Content ID | UP2009-NPUB30061_00-PELFASTCC0000020 |
| Binary | `EBOOT.BIN` → decrypted `EBOOT.ELF` |
| Format | ELF64, big-endian, PowerPC64 (Cell PPU / LV2), statically linked, stripped |
| Size | 2,817,600 bytes decrypted |
| Entry point | 0x1F2B38 |

## How the binary was obtained

Full chain is documented in [notes/PROVENANCE.md](notes/PROVENANCE.md). Short
version: the PSN PKG was extracted, and the NPDRM-protected SELF was decrypted
with **scetool 0.2.9** using the title's license (RAP). The fail0verflow
`unself` cannot handle NPDRM SELFs (key type 0x08), which is why scetool was
needed.

## Pipeline

1. **Decrypt** (done): scetool → `EBOOT.ELF` (verified PPC64 ELF; game strings
   present, e.g. `gCometEnv`, `mCometObjPath`, "© 2009 Pelfast, Inc.").
2. **Analyze** (this repo): Ghidra 12.2 headless, language
   `PowerPC:BE:64:default`, auto-analysis, then a scripted export
   (`scripts/ExportReconstruction.java`) of every function's decompiled C,
   the function inventory, and the string table.
3. **Reconstruct** (ongoing): read the corpus, name subsystems, and write up
   the game's logic in `notes/` and `reconstruction/`.

### A note on REA

REA (Reverse Engineer Anything, `rea-agents` 6.7.0) is installed on the
analysis machine and registered with Antigravity, and its Ghidra engine is the
same one used here — but REA 6.7.0 **rejects this target at intake**
("unsupported ELF architecture") because its artifact layer does not support
PowerPC64/Cell LV2 ELFs, even with `--provider ghidra`. The corpus was
therefore produced by driving Ghidra headless directly. Details and the exact
error record are in [notes/REA.md](notes/REA.md).

## Repo layout

- `analysis/` — machine-generated inventories: `functions.tsv`,
  `strings.tsv`, `summary.txt`, plus the REA attempt records
  (`rea-overview*.json`).
- `decompiled/` — the decompiled C corpus (`decompiled.c`), Ghidra output.
  Function names are `FUN_<address>` unless the binary carried a symbol;
  this is reconstructed logic, not original source.
- `notes/` — human/AI-written findings: provenance, REA notes, and
  reconstruction write-ups as they are produced.
- `reconstruction/` — cleaned-up, renamed, re-typed pseudocode for the
  subsystems we actually understand (grows over time).
- `scripts/` — the exact scripts used to produce the corpus.

Not in git (kept on the analysis machine): the decrypted `EBOOT.ELF`, the
Ghidra project database (`ghidra/`), and the RAP license file — see
`notes/PROVENANCE.md` for where they live and how they were obtained.

## Reproducing the analysis

On the analysis machine (JerryDesktop):

```bash
source ~/tools/env.sh   # JAVA_HOME (Temurin 21), GHIDRA_INSTALL_DIR, Node 24
~/tools/ghidra_12.2_PUBLIC/support/analyzeHeadless ~/comet-crash-re/ghidra CometCrashRE \
  -import ~/Downloads/comet_crash_extracted/EBOOT.ELF \
  -processor PowerPC:BE:64:default -cspec default \
  -scriptPath ~/comet-crash-re/scripts \
  -postScript ExportReconstruction.java ~/comet-crash-re/analysis
```
