#!/bin/bash
# Comet Crash (NPUB30061) static recompilation -- one-shot setup + build.
#
# Usage:
#   ./setup.sh --eboot /path/to/EBOOT.ELF --gamedata /path/to/base-game \
#              [--patchdata /path/to/patch-tree] [--dlcdata /path/to/dlc-tree]
#
# The arguments are the same ones scripts/setup.py takes; they are
# forwarded to it. You supply every game-derived input yourself
# (decrypted EBOOT.ELF and the extracted packages from your own
# copy -- see ../notes/PROVENANCE.md for the decryption recipe).
# Nothing game-derived is in this repository or is downloaded.
#
# What this script does:
#   1. Checks build prerequisites.
#   2. Initializes the ps3recomp submodule (runtime toolkit).
#   3. Runs scripts/setup.py: validates the ELF, lifts the PPU and
#      SPU code into recompiled/ + spu/, and assembles the VFS
#      overlay (base -> 1.10 patch -> DLC) under vfs/.
#      Skipped when a lift already exists; pass --force-lift to redo.
#   4. Configures and builds build/CometCrashRecomp.
#
# Launch afterwards with:  ./run.sh /path/to/EBOOT.ELF
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

FORCE_LIFT=0
ARGS=()
for a in "$@"; do
    if [ "$a" = "--force-lift" ]; then
        FORCE_LIFT=1
    else
        ARGS+=("$a")
    fi
done

echo "[*] Checking prerequisites..."
missing=0
for tool in cmake ninja gcc g++ python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "    [!] missing: $tool"
        missing=1
    fi
done
if [ "$missing" = 1 ]; then
    echo "    Install the build dependencies first (see README.md in this directory)."
    exit 1
fi

if [ ! -f "$REPO_ROOT/third_party/ps3recomp/CMakeLists.txt" ]; then
    echo "[*] Initializing ps3recomp submodule..."
    git -C "$REPO_ROOT" submodule update --init --recursive
fi

if [ "$FORCE_LIFT" = 1 ] || [ ! -d "$SCRIPT_DIR/recompiled" ] || [ ! -d "$SCRIPT_DIR/spu" ]; then
    echo "[*] Running the lift pipeline (scripts/setup.py)..."
    python3 "$SCRIPT_DIR/scripts/setup.py" "${ARGS[@]}"
else
    echo "[*] Lift output already present (recompiled/ + spu/); skipping."
    echo "    Pass --force-lift to regenerate it from the ELF."
    # Still refresh the VFS overlay if data args were given.
    if [ "${#ARGS[@]}" -gt 0 ]; then
        python3 - "$SCRIPT_DIR" "${ARGS[@]}" <<'PYEOF'
import sys
from pathlib import Path
sys.path.insert(0, str(Path(sys.argv[1]) / "scripts"))
import argparse
from setup import setup_vfs
p = argparse.ArgumentParser()
p.add_argument("--eboot", type=Path)
p.add_argument("--gamedata", type=Path)
p.add_argument("--patchdata", type=Path)
p.add_argument("--dlcdata", type=Path)
a, _ = p.parse_known_args(sys.argv[2:])
layers = [x.resolve() for x in (a.gamedata, a.patchdata, a.dlcdata) if x]
if layers:
    setup_vfs(layers, Path(sys.argv[1]))
PYEOF
    fi
fi

echo "[*] Configuring (Ninja, RelWithDebInfo)..."
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

echo "[*] Building CometCrashRecomp..."
cmake --build "$SCRIPT_DIR/build" -j"$(nproc)"

echo ""
echo "[+] Done: $SCRIPT_DIR/build/CometCrashRecomp"
echo "    Launch with:  cd $SCRIPT_DIR && ./run.sh /path/to/EBOOT.ELF"
