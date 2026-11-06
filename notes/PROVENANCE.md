# Provenance: how the decrypted EBOOT.ELF was obtained

Date: 2026-11-05. Machine: JerryDesktop (Bazzite Linux).

## Source material

- PSN package: `UP2009-NPUB30061_00-PELFASTCC0000020-A0110-V0100-PE.pkg`
  (Comet Crash full game, US PSN), already in `~/Downloads`.
- Extracted with the owner's existing PS3 PKG tooling to
  `~/Downloads/comet_crash_extracted/NPUB30061/`, giving
  `USRDIR/EBOOT.BIN` (2,820,336 bytes).

## What EBOOT.BIN turned out to be

- A **SELF** (magic `SCE\0`), not a plain ELF.
- `readself` (fail0verflow ps3tools): app type **NP-DRM application**,
  content ID `UP2009-NPUB30061_00-PELFASTCC0000020`, app version 1.0.0.
- scetool info: key revision 0x0007, FW version 3.50, **NPDRM licence type
  0x00000002 = LOCAL** — i.e. the executable's metadata key is wrapped with
  this title's own license key (klic), so generic keys cannot decrypt it.

## Failed routes (for the record)

- fail0verflow `unself EBOOT.BIN EBOOT.ELF` → `invalid type: 00000008`.
  unself does not support NPDRM SELFs (key type 8).
- scetool with the standard keyset alone → `Could not decrypt header`
  (expected: LOCAL licence needs the title klic).
- scetool with the **free** klic override (`NP_klic_free`) → same failure;
  this title is not free-licence.

## The key material

- scetool 0.2.9 built from source (naehrwert/scetool) at `~/scetool-build/`.
- Keyset/curves from the public `jevinskie/ps3dotdir` collection, staged at
  `~/scetool-data/` (`keys`, `ldr_curves` 7744 B, `vsh_curves` 360 B).
  Gotcha: that `keys` file ships with CRLF line endings; scetool's parser
  rejects every entry until CRs are stripped. scetool finds its data only via
  the `PS3` environment variable pointing at the directory.
- The title's **RAP** (`UP2009-NPUB30061_00-PELFASTCC0000020.rap`, 16 bytes):
  ***REMOVED***'s PS3_GAMES.tsv has **no** RAP for NPUB30061. The file was
  found in two independent public license collections on ***REMOVED***
  (`***REMOVED***` and `***REMOVED***`); the two copies are
  byte-identical, which is why they were trusted. Staged at
  `~/scetool-data/raps/` and in the working folder. The RAP file itself is
  deliberately not committed to this repo.

## Decryption

```bash
export PS3=$HOME/scetool-data
~/scetool-build/scetool -d EBOOT.BIN EBOOT.ELF
# [*] ELF written to EBOOT.ELF.
```

scetool's `rap_to_klicensee` path converts the RAP to the title klic using
only fixed keys (no console IDPS/act.dat needed), unwraps the NPDRM header,
and decrypts the metadata + sections.

## Verification of the output

- `file`: `ELF 64-bit MSB executable, 64-bit PowerPC, version 1 (Cell LV2),
  statically linked, stripped`.
- ELF header: ELF64, big-endian, type EXEC, machine PowerPC64, entry
  0x1F2B38, 5 LOAD segments, section headers present.
- Strings in the output include `NPUB30061`, `c 2009 Pelfast, Inc. All
  Rights Reserved.`, `mCometObjPath`, `gCometEnv` — i.e. real decrypted game
  code/data, not padding or garbage.
- Decrypted size 2,817,600 bytes (SELF was 2,820,336; difference is the
  SELF/NPDRM header overhead).

Locations on the analysis machine:

- `~/Downloads/comet_crash_extracted/EBOOT.ELF` (deliverable copy)
- `~/Downloads/comet_crash_extracted/work/` (working dir: EBOOT.BIN copy,
  EBOOT.ELF, `raps/`)
