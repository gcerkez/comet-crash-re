#!/usr/bin/env python3
"""
setup.py -- Automated lift pipeline and VFS builder for Comet Crash (NPUB30061).

This script performs a clean, reproducible static recompilation of Comet Crash:
  1. Validates the decrypted EBOOT.ELF binary against known SHA-256 hashes.
  2. Runs ps3recomp PPU loader, function discovery, and C++ code generator.
  3. Extracts the 2 embedded SPU ELFs and 9 SPURS job binaries directly from EBOOT.ELF.
  4. Lifts all 11 SPU binaries and generates spu_workloads.c for fingerprint dispatch.
  5. Populates the Virtual File System (VFS) with game data assets.

Requirements:
  - Python 3.8+
  - ps3recomp submodule (git submodule update --init --recursive)
  - Legally obtained, decrypted EBOOT.ELF from Comet Crash (PSN NPUB30061)
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = "a18d8e4a4a2620fd58f7c09e9639f3de91b3197cab3877c554e369dee430a00c"
EXPECTED_SIZE = 2817600

# SPU ELF metadata (offset in EBOOT.ELF, length, symbol name, entry offset)
SPU_ELFS = [
    {
        "name": "spu_0000_at_00175D00",
        "offset": 0x175E00,
        "size": 68480,
        "entry_sym": "spu_0000_at_00175D00_spu_func_000000E0",
        "fp_full": "0x766D7BD517152191ULL",
        "fp_text": "0x36E2D432684905ACULL",
        "img_id": 1,
    },
    {
        "name": "spu_0001_at_001C0B80",
        "offset": 0x1C0C80,
        "size": 23040,
        "entry_sym": "spu_0001_at_001C0B80_spu_func_00003050",
        "fp_full": "0x57B37E2E758AFC4CULL",
        "fp_text": "0x6D30AD4595732254ULL",
        "img_id": 2,
    },
]

# Raw SPURS job binaries embedded in EBOOT.ELF
SPURS_JOBS = [
    {
        "name": "spu_job_00186980",
        "offset": 0x186980,
        "size": 2192,
        "fp": "0x899936222D9C48EFULL",
        "img_id": 3,
        "extra_args": [],
    },
    {
        "name": "spu_job_00187280",
        "offset": 0x187280,
        "size": 3104,
        "fp": "0x552BED5350500A0BULL",
        "img_id": 4,
        "extra_args": [],
    },
    {
        "name": "spu_job_00187F00",
        "offset": 0x187F00,
        "size": 928,
        "fp": "0xA99398FC8A172503ULL",
        "img_id": 5,
        "extra_args": [],
    },
    {
        "name": "spu_job_00188300",
        "offset": 0x188300,
        "size": 177376,
        "fp": "0xE219CE9055112965ULL",
        "img_id": 6,
        "extra_args": ["--code-end", "0x25870"],
    },
    {
        "name": "spu_job_001B3800",
        "offset": 0x1B3800,
        "size": 2720,
        "fp": "0xC7A6623801C98E3AULL",
        "img_id": 7,
        "extra_args": [],
    },
    {
        "name": "spu_job_001B4300",
        "offset": 0x1B4300,
        "size": 74624,
        "fp": "0x9595A1B696C3C748ULL",
        "img_id": 8,
        "extra_args": [],
    },
    {
        "name": "spu_job_001C6680",
        "offset": 0x1C6680,
        "size": 800,
        "fp": "0xDD4E2377E88BC43EULL",
        "img_id": 9,
        "extra_args": [],
    },
    {
        "name": "spu_job_001C6A00",
        "offset": 0x1C6A00,
        "size": 11392,
        "fp": "0x0844F1A2BB46564BULL",
        "img_id": 10,
        "extra_args": [],
    },
    {
        "name": "spu_job_001C9680",
        "offset": 0x1C9680,
        "size": 29952,
        "fp": "0x070EC5CFD19E1CAFULL",
        "img_id": 11,
        "extra_args": [],
    },
]


def run_cmd(cmd, desc=""):
    if desc:
        print(f"[*] {desc}")
    print(f"    $ {' '.join(str(x) for x in cmd)}")
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"[!] Error: command exited with code {res.returncode}")
        sys.exit(res.returncode)


def validate_eboot(path: Path) -> bytes:
    if not path.is_file():
        print(f"[!] Error: EBOOT.ELF not found at {path}")
        sys.exit(1)

    data = path.read_bytes()
    size = len(data)
    h = hashlib.sha256(data).hexdigest()

    print(f"[*] EBOOT.ELF validation:")
    print(f"    Path:    {path}")
    print(f"    Size:    {size:,} bytes")
    print(f"    SHA-256: {h}")

    if size != EXPECTED_SIZE:
        print(f"[!] Warning: Expected size {EXPECTED_SIZE:,} bytes, got {size:,} bytes.")
    if h != EXPECTED_SHA256:
        print(f"[!] Warning: Expected SHA-256 {EXPECTED_SHA256}, got {h}.")
        print("    Continuing anyway -- proceed with caution if your binary is modified.")
    else:
        print("    Verification SUCCESS: Official decrypted EBOOT.ELF confirmed.")

    return data


def lift_ppu(eboot_path: Path, sdk_dir: Path, out_dir: Path):
    print("\n" + "=" * 60)
    print("  STEP 1: PPU Recompilation (ppc64 -> C++)")
    print("=" * 60)

    ppu_loader = sdk_dir / "tools" / "ppu_loader.py"
    find_functions = sdk_dir / "tools" / "find_functions.py"
    ppu_lifter = sdk_dir / "tools" / "ppu_lifter.py"

    inter_dir = out_dir / "out"
    recomp_dir = out_dir / "recompiled"
    inter_dir.mkdir(parents=True, exist_ok=True)
    recomp_dir.mkdir(parents=True, exist_ok=True)

    img_prefix = inter_dir / "EBOOT"
    image_json = img_prefix / "EBOOT.image.json"
    funcs_json = img_prefix / "EBOOT.functions.json"
    imports_json = img_prefix / "EBOOT.imports.json"
    full_funcs_json = inter_dir / "functions_full.json"

    # Step 1a: Loader
    run_cmd(
        [sys.executable, str(ppu_loader), str(eboot_path), "-o", str(img_prefix)],
        "Loading ELF segments, exports, and imports...",
    )

    # Step 1b: Function discovery (the tool reads the ELF itself;
    # the loader's function list seeds it via --seed-json)
    run_cmd(
        [
            sys.executable,
            str(find_functions),
            str(eboot_path),
            "--seed-json",
            str(funcs_json),
            "--json",
            "--output",
            str(full_funcs_json),
        ],
        "Discovering PPU subroutines and basic blocks...",
    )

    # Step 1c: Lift PPU to C++
    run_cmd(
        [
            sys.executable,
            str(ppu_lifter),
            str(eboot_path),
            "--functions",
            str(full_funcs_json),
            "--hle-stubs",
            str(imports_json),
            "--jobs",
            str(os.cpu_count() or 4),
            "--output",
            str(recomp_dir),
        ],
        "Lifting PPU functions into C++ source chunks...",
    )


def lift_spu(eboot_data: bytes, sdk_dir: Path, out_dir: Path):
    print("\n" + "=" * 60)
    print("  STEP 2: SPU Extraction & Recompilation")
    print("=" * 60)

    spu_dir = out_dir / "spu"
    spu_dir.mkdir(parents=True, exist_ok=True)

    find_spu_funcs = sdk_dir / "tools" / "find_spu_functions.py"
    spu_lifter = sdk_dir / "tools" / "spu_lifter.py"

    # 2a: Lift the 2 embedded SPU ELFs
    for elf in SPU_ELFS:
        target_dir = spu_dir / elf["name"]
        target_dir.mkdir(parents=True, exist_ok=True)
        raw_path = target_dir / "image.elf"
        raw_path.write_bytes(eboot_data[elf["offset"] : elf["offset"] + elf["size"]])

        funcs_json = target_dir / "functions.json"
        lift_json = target_dir / "lift.json"

        run_cmd(
            [sys.executable, str(find_spu_funcs), str(raw_path), "--out", str(funcs_json)],
            f"Analyzing SPU ELF {elf['name']}...",
        )
        run_cmd(
            [
                sys.executable,
                str(spu_lifter),
                str(raw_path),
                "--functions",
                str(funcs_json),
                "--output",
                str(target_dir),
                "--symbol-prefix",
                f"{elf['name']}_",
                "--report",
                str(lift_json),
            ],
            f"Lifting SPU ELF {elf['name']} to C...",
        )

    # 2b: Lift the 9 raw SPURS job binaries
    for job in SPURS_JOBS:
        target_dir = spu_dir / job["name"]
        target_dir.mkdir(parents=True, exist_ok=True)
        raw_path = target_dir / "job.bin"
        raw_path.write_bytes(eboot_data[job["offset"] : job["offset"] + job["size"]])

        job_funcs_json = target_dir / "functions.json"
        run_cmd(
            [
                sys.executable,
                str(find_spu_funcs),
                "--raw",
                "--base",
                "0",
                str(raw_path),
                "--out",
                str(job_funcs_json),
            ],
            f"Analyzing SPURS job {job['name']}...",
        )
        cmd = [
            sys.executable,
            str(spu_lifter),
            str(raw_path),
            "--functions",
            str(job_funcs_json),
            "--output",
            str(target_dir),
            "--symbol-prefix",
            f"{job['name']}_",
        ] + job["extra_args"]

        run_cmd(cmd, f"Lifting SPURS Job {job['name']} (0x{job['offset']:08X})...")

    # 2c: Generate spu_workloads.c
    print("[*] Generating spu_workloads.c dispatch table...")
    lines = [
        "/* spu_workloads.c -- Generated by recomp/scripts/setup.py */",
        '#include "spu_workload.h"',
        "",
        "extern void spu_begin_image(int image_id);",
        "",
        "/* SPU ELF declarations */",
    ]

    for elf in SPU_ELFS:
        lines.append(f"extern void {elf['entry_sym']}(spu_context*);")
        lines.append(f"extern void {elf['name']}_spu_recomp_register(void);")

    lines.append("\n/* SPURS Job declarations */")
    for job in SPURS_JOBS:
        lines.append(f"extern void {job['name']}_spu_func_00000000(spu_context*);")
        lines.append(f"extern void {job['name']}_spu_recomp_register(void);")

    lines.append("\nvoid spu_register_all(void)")
    lines.append("{")

    for elf in SPU_ELFS:
        lines.append(f"    spu_begin_image({elf['img_id']}); {elf['name']}_spu_recomp_register();")
        lines.append(f"    spu_workload_register_img({elf['fp_full']}, {elf['entry_sym']}, {elf['img_id']}, \"{elf['name']}\");")
        lines.append(f"    spu_workload_register_img({elf['fp_text']}, {elf['entry_sym']}, {elf['img_id']}, \"{elf['name']}.text\");")

    for job in SPURS_JOBS:
        lines.append(f"    spu_begin_image({job['img_id']}); {job['name']}_spu_recomp_register();")
        lines.append(f"    spu_workload_register_img({job['fp']}, {job['name']}_spu_func_00000000, {job['img_id']}, \"{job['name']}\");")

    lines.append("    spu_begin_image(0);")
    lines.append("}")
    lines.append("")
    lines.append("__attribute__((constructor)) static void spu_register_all_ctor(void)")
    lines.append("{")
    lines.append("    spu_register_all();")
    lines.append("}")
    lines.append("")

    workloads_c = spu_dir / "spu_workloads.c"
    workloads_c.write_text("\n".join(lines))
    print(f"    Wrote {workloads_c}")


def _link_or_copy(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() or dst.is_symlink():
        if dst.is_dir() and not dst.is_symlink():
            shutil.rmtree(dst)
        else:
            dst.unlink()
    try:
        dst.symlink_to(src.resolve())
    except OSError:
        if src.is_dir():
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)


def setup_vfs(gamedata_layers, out_dir: Path):
    print("\n" + "=" * 60)
    print("  STEP 3: Virtual File System Setup (VFS)")
    print("=" * 60)

    vfs_dir = out_dir / "vfs"
    vfs_game = vfs_dir / "PS3_GAME"
    vfs_usrdir = vfs_game / "USRDIR"
    vfs_usrdir.mkdir(parents=True, exist_ok=True)

    layers = [p for p in (gamedata_layers or []) if p and p.exists()]
    if not layers:
        print("[!] No game data directories provided or found.")
        print(f"    VFS directory created at: {vfs_dir}")
        print(f"    Please place PARAM.SFO in {vfs_game}/ and game assets (USRDIR/) in {vfs_usrdir}/.")
        return

    # Overlay order: base package first, then the patch tree, then
    # DLC -- later layers win per file, matching how the working
    # VFS was assembled (notes 037).
    for layer in layers:
        print(f"[*] Overlaying {layer} -> {vfs_dir}...")
        for cand in [layer / "PARAM.SFO", layer / "PS3_GAME" / "PARAM.SFO"]:
            if cand.is_file():
                _link_or_copy(cand, vfs_game / "PARAM.SFO")
                print(f"    PARAM.SFO <- {cand}")
                break
        src_usrdir = None
        for cand in [layer / "USRDIR", layer / "PS3_GAME" / "USRDIR"]:
            if cand.is_dir():
                src_usrdir = cand
                break
        if src_usrdir:
            count = 0
            for item in sorted(src_usrdir.rglob("*")):
                if item.is_file():
                    _link_or_copy(item, vfs_usrdir / item.relative_to(src_usrdir))
                    count += 1
            print(f"    {count} files from {src_usrdir}")


def main():
    parser = argparse.ArgumentParser(
        description="Lift Comet Crash (NPUB30061) into a buildable C++ recompilation project."
    )
    parser.add_argument(
        "--eboot",
        type=Path,
        required=True,
        help="Path to decrypted EBOOT.ELF binary from Comet Crash",
    )
    parser.add_argument(
        "--gamedata",
        type=Path,
        default=None,
        help="Path to extracted game directory containing USRDIR/ and PARAM.SFO",
    )
    parser.add_argument(
        "--patchdata",
        type=Path,
        default=None,
        help="Path to extracted 1.10 patch tree (overlaid over --gamedata)",
    )
    parser.add_argument(
        "--dlcdata",
        type=Path,
        default=None,
        help="Path to extracted Bionic Swarm DLC tree (overlaid last)",
    )
    parser.add_argument(
        "--sdk",
        type=Path,
        default=None,
        help="Path to ps3recomp SDK checkout (defaults to ../third_party/ps3recomp)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output project directory (defaults to parent of scripts/)",
    )

    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    recomp_dir = args.out.resolve() if args.out else script_dir.parent
    sdk_dir = (
        args.sdk.resolve()
        if args.sdk
        else (recomp_dir.parent / "third_party" / "ps3recomp").resolve()
    )

    print("============================================================")
    print("  Comet Crash (PS3) Static Recompilation Setup Pipeline")
    print("============================================================")
    print(f"Project directory: {recomp_dir}")
    print(f"ps3recomp SDK:     {sdk_dir}")
    print(f"Input EBOOT.ELF:   {args.eboot}")
    if args.gamedata:
        print(f"Input Game Data:   {args.gamedata}")

    if not sdk_dir.is_dir():
        print(f"[!] Error: ps3recomp SDK not found at {sdk_dir}.")
        print("    Run: git submodule update --init --recursive")
        sys.exit(1)

    eboot_data = validate_eboot(args.eboot.resolve())
    lift_ppu(args.eboot.resolve(), sdk_dir, recomp_dir)
    lift_spu(eboot_data, sdk_dir, recomp_dir)
    layers = [
        a.resolve()
        for a in (args.gamedata, args.patchdata, args.dlcdata)
        if a is not None
    ]
    setup_vfs(layers, recomp_dir)

    print("\n" + "=" * 60)
    print("  SETUP COMPLETE! Next steps to build:")
    print("=" * 60)
    print(f"  cd {recomp_dir}")
    print("  cmake -B build -G Ninja")
    print("  cmake --build build")
    print("  ./run.sh")
    print("=" * 60 + "\n")


if __name__ == "__main__":
    main()
