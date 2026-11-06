# Reconstruction notes 020 — corpus completion: the missing
two-thirds, and the 101-way dispatcher

Date: 2026-11-06. Jerry's call: run the boundary-free sweep
across the whole binary and recover whatever else is missing.
He was right — there was a lot.

## The census

The corpus's 868 functions covered only 33.0% of the
1,453,616-byte code segment: 976,695 bytes in 303 gaps.
Sampling gap starts showed ordinary function prologues
everywhere — the gaps were real, uncarved code (the original
Ghidra pass had carved mainly what direct-call flow from the
entry reached).

## How the missing functions were found (boundary-free)

Entry candidates were the union of:
- the binary's own OPD function-pointer table: 829 of its
  851 entries point into the gaps — it is effectively a map
  of the address-taken functions (one 92 KB gap at
  0x5262C-0x68D94 alone holds 165 of them at regular strides
  — a statically linked library block);
- bl (call) targets landing in gaps, and LR-save prologues
  following returns, from a linear Capstone sweep
  (scripts/census_gaps.py).

1,726 candidates → scripts/CreateRegionFunctions.java in
three batches: 1,444 functions created. Then the hub (below)
added its handlers.

## The hub is a dispatcher, not a function

Forcing 0xF6990's body to the whole 25,560-byte span made
the decompiler reveal the truth: FUN_000f6990 is a 101-way
jump-table dispatcher (renamed **op_dispatch**):

    op_dispatch(ctx, opcode, a, b): if (opcode < 101)
        jump table[opcode]     // table at 0xF6A28, offsets
                               // relative to its own base

The code after it (0xF6BBC-0xFA2F8) is the 69 distinct
handler functions, reachable only through the table — which
is why no call-target, prologue, or OPD heuristic could see
them, and why the asset-registry consumers "didn't exist":
the handlers are the registry's users (they walk its fields
at 0x1F5E00+). Callers pass meaningful opcodes:
op_dispatch(ctx, 6, 8..11, 0), op_dispatch(ctx, 0x13, 0, 0)
— a command/event system at the game's core. Ten functions
call level_load_file: nine dispatch handlers (level loading
is itself dispatched per mode/context) plus FUN_000fa2f8
(the level-select logic).

## Corpus v3

- **2,646 functions, 2,646 decompiled, 0 failed**
  (was 868). decompiled.c: 7.16 MB.
- Coverage: 91.2% of the code segment. Remaining 129 KB in
  180 gaps: jump tables, constant pools, alignment — plus
  one 13 KB candidate-free stretch at 0x106B58-0x109E58
  still to explain.
- Function→string refs: 746 across 205 functions.
- Biggest recovered: FUN_00124e80 (31,272 B), FUN_00146b98
  (23,224 B), FUN_0014f360 (16,496 B — the former
  "FUN_0014f35c" mystery region, now entered correctly),
  FUN_00112a80 (15,556 B — particle/starfield setup:
  references turbulence.dds, star_shader, particle shaders),
  FUN_000e5808 (12,840 B — references "gTeamCol[%d]").

## What this unlocks

The port's code-truth work (camera, HUD, mission setup) now
has its source material: the dispatch handlers and the game
code around game_loop are in the corpus and readable.
Reading order: the level-loading handlers and FUN_000fa2f8
(mission flow), FUN_000e1e58 (an op_dispatch caller beside
game_loop — likely the command queue), then the
arenaGraphics neighborhood (0xFF9D4+ also gained functions).
