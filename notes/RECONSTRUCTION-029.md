# Reconstruction notes 029 — Jerry's PS3 RE resource list,
evaluated against this project

Date: 2026-11-06. Each resource was read and, where
possible, tested against the actual corpus.

## clienthax/Ps3GhidraScripts — YES, paid off immediately

Cloned to ~/tools/ps3-ghidra-scripts on the desktop.
- data/nids.txt (9,100 FNID->name pairs) named ALL 25 of
  our remaining unnamed imports: import map is now
  195/195. Highlights: the 10 "cellSysutil" stubs are the
  cellVideoOut/cellAudioOut configuration family (the
  display-mode setters); sys_io completed with
  cellPadSetPortSetting + cellPadSetActDirect (actuator/
  rumble, called from pads_sample); sysPrxForUser gave
  sys_ppu_thread_create (called from 0xA1CF8, 0xAE424,
  0x1650BC — the preloader thread of notes 026 is now
  import-verified), sys_ppu_thread_exit and
  sys_spu_image_close. sceNp/cellVideoExportUtility/
  sceNpCommerce2 names recorded (PSN stays stubbed).
  Applied via NameImports.java; corpus re-exported;
  import_callers.tsv shows the new names at call sites.
- data/syscall.txt (798 lines) on deck; low need (this
  title calls PRX imports, not raw syscalls).
- AssignPs3R2FromOpd.java (assigns r2 per function from
  the OPD table) and FindPs3JumptableTargets.py (switch
  table recovery) remain useful utilities; our pipeline
  already covers their ground by hand.
- Its README's language + cspec recipe was A/B tested
  (below).

## The forks (28) — nothing new for us

Spot-checked the three with substantive-looking commit
heads: RipleyTom (Ghidra-version packaging builds),
MilchRatchet (personal experiments), ZephyrCodesStuff
(Ghidra API modernization + README fixes). No fork adds
PS3 knowledge beyond upstream; Zephyr's would matter
only if the upstream scripts hit API breaks on a future
Ghidra.

## garyttierney/ghidra-batteries-included + issue #5

The distro is Ghidra 10.1-era — obsolete next to our
12.2. Issue #5 is literally its "PS3 support" thread:
the plan was to bundle clienthax's PPU/SPU modules, the
r2 cspec patch and a SELF analyzer, and the thread is
where clienthax's r2 fix circulates. The one transferable
component is cpp-class-analyzer (bundled there, available
standalone for modern Ghidra) — a maybe-later for vtable
work; our GUI vtables are already mapped by hand.

## NSA/ghidra issue #570 — explains our decompile noise

"TOC/OPD/GOT is not processed in ELFs using PPC64 with
32-bit addressing" (2019). It documents exactly why our
decompiles carry unaff_r2: r2 save/restore around
indirect (virtual) calls makes the decompiler treat the
TOC register as a variable. Its suggested workaround
(NOP-ing the r2 restores) means patching the binary —
not applicable to a read-only corpus. Diagnostic value
only; it validates the python-side r2-offset resolution
our tooling already does.

## aerosoul94/ida_gel — no

IDA Pro loaders for PS3/Vita/WiiU ELFs (IDA SDK, VS2015
era). We have no IDA seat and our Ghidra pipeline already
handles everything a loader would (ELF, OPDs, stubs,
FNIDs). Nothing portable in the repo for us.

## A/B: 32addr language + the r2 cspec patch

Scratch imports of EBOOT.ELF (main project untouched):
- Language PowerPC:BE:64:64-32addr alone: the matrix
  composer decompiles with unaff_r2 = 0 and absolute
  matrix-row stores (param_1 + 0x54dfb0/0x54dfc0) — much
  cleaner. camera_computeView unchanged (unaff_r2 = 55
  either way; its r2 churn is the issue-#570 pattern).
  The composer's halt_baddata truncation persists under
  both languages (a pcode-level rejection in its tail;
  Capstone decodes every word there, so it is not a
  missing opcode in the listing).
- Adding clienthax's cspec change (r2 in <unaffected>
  of ppc_64_32.cspec): no help for computeView (55) and
  the composer got WORSE (unaff_r2 0 -> 17). The recipe
  pairs with his custom sleigh module, not stock 12.2.
  Patch reverted; desktop Ghidra is stock again.
CONCLUSION: the 32addr language is the documented-right
choice for PS3 ELFs and a corpus-v4 candidate, but the
payoff is readability in a subset of functions — not
worth orphaning the current corpus's carves/renames
today. Decision left to Jerry with this evidence.

## rpcs3/bninja-ps3 — no (verdict from the project page,
supplied by Jerry after the GitLab fetch was bot-walled)

Valters Tomsons' Cell PPU ELF loader for Binary Ninja
4.x/5.x (MIT, actively maintained; GitHub mirror at
valters-tomsons/bninja-ps3). It ships its own nids and
syscalls data files and an AssertToc helper — but its
Resources list shows the same lineage already harvested
here: clienthax/Ps3GhidraScripts, RPCS3, psdevwiki,
xerpi's nidreader gist. Nothing left for it to give:
our import map is already 195/195. Decisive anyway:
its own Known Issues state Binary Ninja does not lift
many PPC instructions, and the unsupported list names
vmaddfp, stvx, lfs/stfs/lfd/stfd, fcmpu, clrldi and
lwarx/stwcx — precisely the families saturating our
VMX-heavy functions (the matrix composer above all).
Ghidra lifts those fine. Plus BN is commercial and its
import/syscall definitions carry no arguments.
