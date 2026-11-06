# Reconstruction notes 003 — boot walk: entry, CRT startup, and main

Date: 2026-11-05. The startup chain is walked and named in the Ghidra
project (`scripts/BootWalk.java`, `scripts/RenameFunctions.java`,
`scripts/renames.csv`; chain dump in `analysis/boot_chain.txt`).

## The chain

```
_entry (0x10230, 36 B)            <- ELF header entry OPD target
  -> crt_startup (0x10338, 468 B) <- CRT glue
       -> main (0xDAE30, 3,548 B) <- the game's main()
  -> exit thunk (0x174844)
```

- **`_entry`** does exactly two things: call `crt_startup`, then call
  the exit thunk with 0. Ghidra's initial analysis had not created a
  function at the entry code address at all; BootWalk created it.
- **`crt_startup`** is the SDK's C runtime glue, identifiable by its
  shape: it copies the two argument arrays it was handed (the
  argc/argv/envp material), stores argc/argv into TOC globals, runs a
  three-entry initializer table through thunks, and finishes with
  `result = main(argc, argv, envp)` feeding the exit path
  (`FUN_000b6654`, still unnamed).
- **`main`** is the game's own `main` — 3.5 KB containing the
  subsystem initialization sequence, including the `cellPadInit` call
  found in the FNID pass. The earlier reading of `FUN_000dae30` as a
  "pad init function" is corrected by this walk: pad init is one step
  inside main, which is also why main is the only caller.

## The input seam, refined

- `main` → … → **`pads_update` (0x111278, 1,128 B)**: the per-frame
  input update. It calls the `cellPadGetInfo2` thunk (connection state)
  and `pads_sample`.
- **`pads_sample` (0x110738, 2,880 B)**: per-pad sampling. With the
  thunk renames below, its body now reads in the corpus as real code —
  e.g. `iVar21 = cellPadGetData(iVar21, local_110);`.

## Import thunks renamed (77)

Game code calls imports through one stub thunk per function in the
small executable section at `0x173084+`. `RenameFunctions.java`
resolved each named import's thunk (references to its table slot) and
renamed it to the SDK name — `cellPadGetData`, `cellFsOpen`, … — so
call sites across the whole corpus now read as direct calls to the
named APIs. (77 of 134 named imports have a distinct thunk; the rest
are reached another way and keep their slot names.)

Corpus re-exported after renaming: **844 functions, 844 decompiled,
0 failed**; `decompiled/decompiled.c` and the by-module split are
regenerated from it.

## Process fix

`ExportCorpus2.java` no longer writes `function_strings.tsv`: its
Ghidra-side version cannot see this binary's TOC string references and
silently overwrote the Capstone-produced map with a header-only file
during this pass (caught when the module split came back all
"unassigned"; the map was regenerated and the split redone). The
Capstone script is the single source for that file.

## Module map, refreshed counts

| Module | Functions |
|---|---|
| engine_multistream | 52 |
| engine_psgl | 19 |
| game_local_text_cpp | 8 |
| runtime_c_library | 17 |
| unassigned | 748 |

(Counts shifted slightly vs. notes 002: the renamed thunks changed
call-graph edges the one-hop propagation walks.)

## Next steps

1. Walk `main` itself: identify the init order and the main loop (its
   callees include the render and audio init paths), and find where
   `gCometEnv` (0x180390) is populated.
2. Name the frame loop around `pads_update` — its caller chain leads
   to the game's per-frame update, the future home of the native
   frame loop.
3. Keep growing the module map from import-call-site seeds
   (cellGcmSys/cellResc callers are renderer-adjacent, and so on).
