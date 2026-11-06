#!/usr/bin/env python3
"""Split the decompiled corpus into per-module files by evidence.

Decompilation projects organize code by the original translation units.
We don't have the original build files, so each function is assigned to
a module from the strings it references (function_strings.tsv from the
Ghidra export):

  * A function that references an assert string containing a leaked
    source filename (arenaGraphics.cpp, ObjectGroup.cpp, ...) belongs
    to that game module.
  * A function that references a cellMS* name-table string or other
    MultiStream text belongs to the statically linked MultiStream
    audio engine; likewise PSGL text for the PSGL renderer.
  * A function that references the Dinkumware C library's build paths
    belongs to the C runtime.
  * Everything else is honestly "unassigned" until call-graph work
    clusters it.

Usage: split_modules.py <analysis/v2 dir> <out dir>
Writes <out>/<module>.c per module and <out>/manifest.tsv
(function, module, evidence).
"""

import os
import re
import sys
from collections import defaultdict

GAME_FILES = [
    "arenaGraphics.cpp",
    "ObjectGroup.cpp",
    "local_text.cpp",
    "fontMapFormat.cpp",
    "sound/engine.cpp",
    "main.c",
    "GameSpot.c",
]


def classify(strings: list[str]) -> tuple[str, str]:
    for s in strings:
        for gf in GAME_FILES:
            if gf in s:
                mod = "game_" + gf.replace("/", "_").replace(".", "_")
                return mod, f"assert string names {gf}"
    for s in strings:
        if s.startswith("cellMS") or "MultiStream" in s or "multiStream" in s:
            return "engine_multistream", "references MultiStream name/text"
    for s in strings:
        if "PSGL" in s or "psgl" in s or "_js" in s or "OpenGL" in s:
            return "engine_psgl", "references PSGL text"
    for s in strings:
        if "dinkumware" in s or "/home/kanee/" in s:
            return "runtime_c_library", "references Dinkumware build path"
    return "unassigned", ""


def main() -> int:
    v2_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    func_strings: dict[str, list[str]] = defaultdict(list)
    with open(os.path.join(v2_dir, "function_strings.tsv")) as f:
        next(f)
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 3:
                func_strings[parts[0]].append(parts[2])

    decompiled = open(os.path.join(v2_dir, "decompiled.c")).read()
    # Split into the file header + one block per function. Blocks begin
    # with the banner comment the export script writes.
    banner = re.compile(r"\n/\* ={10,}\n   (\S+) @ ([0-9a-f]+)  \((\d+) bytes\)\n   ={10,} \*/\n")
    parts = banner.split(decompiled)
    header = parts[0]
    blocks: dict[str, str] = {}
    order: list[str] = []
    for i in range(1, len(parts), 4):
        _name, addr, _size = parts[i], parts[i + 1], parts[i + 2]
        body = parts[i + 3]
        blocks[addr] = body
        order.append(addr)

    by_module: dict[str, list[str]] = defaultdict(list)
    evidence: dict[str, tuple[str, str]] = {}
    for addr in order:
        mod, why = classify(func_strings.get(addr, []))
        by_module[mod].append(addr)
        evidence[addr] = (mod, why)

    # One hop of call-graph propagation: a function that calls (or is
    # called by) a strongly classified function probably belongs to the
    # same module. Labeled as probable in the manifest; never chained.
    call_re = re.compile(r"\bFUN_([0-9a-f]{8})\s*\(")
    strong = {a: evidence[a][0] for a in order if evidence[a][0] != "unassigned"}
    neighbors: dict[str, str] = {}
    for addr in order:
        for callee in call_re.findall(blocks[addr]):
            if callee in strong and addr not in strong:
                neighbors.setdefault(addr, strong[callee])
            if addr in strong and callee not in strong:
                neighbors.setdefault(callee, strong[addr])
    for addr, mod in neighbors.items():
        if evidence[addr][0] == "unassigned" and addr in blocks:
            by_module["unassigned"].remove(addr)
            by_module[mod].append(addr)
            evidence[addr] = (mod, "call-graph neighbor of a classified function (probable)")

    total_bytes = 0
    for mod, addrs in sorted(by_module.items()):
        path = os.path.join(out_dir, mod + ".c")
        with open(path, "w") as f:
            f.write(f"/* Comet Crash — module split: {mod} ({len(addrs)} functions)\n")
            f.write("   Split from the Ghidra corpus by string-reference evidence;\n")
            f.write("   see manifest.tsv for the per-function evidence. */\n")
            for addr in addrs:
                f.write(f"\n/* ---- function @ {addr} ---- */\n")
                f.write(blocks[addr])
        print(f"{mod:28s} {len(addrs):4d} functions")

    with open(os.path.join(out_dir, "manifest.tsv"), "w") as f:
        f.write("function\tmodule\tevidence\n")
        for addr in order:
            mod, why = evidence[addr]
            f.write(f"{addr}\t{mod}\t{why}\n")
    print(f"total: {len(order)} functions split")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
