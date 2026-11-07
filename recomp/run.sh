#!/bin/bash
# Comet Crash (NPUB30061) - Static Recompilation Launcher
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$SCRIPT_DIR/build/CometCrashRecomp"

if [ ! -f "$BIN" ]; then
    echo "[!] Executable not found at $BIN."
    echo "    Please build the project first:"
    echo "      cd $SCRIPT_DIR && cmake -B build -G Ninja && cmake --build build"
    exit 1
fi

export PS3_VFS_ROOT="${PS3_VFS_ROOT:-$SCRIPT_DIR/vfs}"
export PS3_TITLE="Comet Crash (ps3recomp)"
export PS3RECOMP_VK_WINDOW=1
export PS3RECOMP_VK_GUEST_PROGRAMS=1
export PS3RECOMP_SPIRV_CACHE="${PS3RECOMP_SPIRV_CACHE:-/tmp/spirv_cache}"
export VK_DRAW_BATCH=1
export PS3_VIDEO_RES="${PS3_VIDEO_RES:-720}"
export MSGDIALOG_ANSWER=no

echo "[*] Launching Comet Crash Recomp..."
echo "    VFS:  $PS3_VFS_ROOT"
echo "    Exec: $BIN"

exec "$BIN" "$@"
