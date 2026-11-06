# Reconstruction notes 025 — the missing 13 KB is one
function: the entity transform pass

Date: 2026-11-06. The census's one unexplained stretch
(0x106B58-0x109E58, 13 KB, zero entry candidates) is now
carved and decompiled: FUN_00106b58, 13,012 bytes, 1,808
lines, renamed arena_entityTransformPass. Corpus: 2,647
functions; code coverage 91.2% -> 92.1% (remaining gaps
are 5-7 KB data stretches with no entry candidates).

## How it was found

The model-registry sweep (slots 0x1F6100-0x1F6450) showed
90 references from uncarved addresses at 0x106C84+,
stepping by exactly 256 bytes and touching consecutive
registry slots — a giant unrolled body inside the gap.
CreateRegionFunctions at 0x106B58 recovered it whole.

## What it is

- NO GL calls, no matrix stack, no string refs: pure
  computation — VMX vector ops throughout (dozens of
  32-byte vector temporaries).
- Calls FUN_00124e80 (31,272 B — the corpus's largest
  function) 50 times, FUN_000ba6e4 x4, assorted math
  helpers, and the render-list sorter FUN_00104890 once
  (notes 022) — consistent with: transform every entity
  (per-entity math core = FUN_00124e80), emit render
  records, sort them.
- Walks the asset registry (the 0x1F61xx struct) — the
  models it transforms are the registry's.

## The pipeline model (evidence-based)

Combined with notes 023/024: the scene renderer is a
CPU-transform pipeline under the shared orthographic
view — entities are projected by VMX math (this pass +
FUN_000ff9d8) into a render list, the list is
painter-sorted (FUN_00104890), and pre-transformed
geometry is submitted to GL; fullscreen post passes
(arena_postChain) finish the frame. There is no GL
modelview camera: the arena's tilt lives in this
transform math (constants/vectors in 106b58 and its core
callee FUN_00124e80), which is the next read.

## Also identified

FUN_00105d80 (3,544 B) touches the registry 28 times and
ends immediately before the transform pass — the
registry's other major consumer (loader/sibling pass).
FUN_00124e80 (31 KB, still unnamed) is now the largest
unidentified function and the transform pass's core.
