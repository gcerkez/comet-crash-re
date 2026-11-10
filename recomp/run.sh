#!/bin/bash
# Comet Crash (NPUB30061) - Static Recompilation Launcher
#
# Runs build/CometCrashRecomp (from ./setup.sh) with the current defaults:
# Vulkan guest programs + draw batching, 1080p, and everything the build
# carries -- SPU job staging and the depth-sort override, ground-shadow
# point sprites, the G8B8 shadow-atlas byte order (main.cpp sets it).
#
# Switches (optional, in front of ./run.sh):
#   RSX_PRIM_STATS=1          log fps + draws per primitive type once a second
#   PS3RECOMP_ENG_POINTS=0    draw points the old way (1 px, no sprites)
#   PS3RECOMP_G8B8_SWAP=0     the runtime's old G8B8 byte order
#   PS3_NO_JOB_OVERRIDE=1     run the lifted SPU geometry job instead of the override
#   CC_BUILD=<dir>            run another build directory (default: build)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$SCRIPT_DIR/${CC_BUILD:-build}/CometCrashRecomp"

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
export PS3_VIDEO_RES="${PS3_VIDEO_RES:-1080}" PAD_STICK_BOOST="${PAD_STICK_BOOST:-1.25}"
export MSGDIALOG_ANSWER=no
export PAD_DEBUG_LOG="${PAD_DEBUG_LOG:-0}"
export CC_CAM_DIAG="${CC_CAM_DIAG:-0}"
export CC_LOCK_ZOOM="${CC_LOCK_ZOOM:-0}"
export CC_FORCE_OVERVIEW="${CC_FORCE_OVERVIEW:-0}"
export CC_FORCE_FOLLOW="${CC_FORCE_FOLLOW:-0}"
export PS3RECOMP_VP_DIAG="${PS3RECOMP_VP_DIAG:-0}"

echo "[*] Launching Comet Crash Recomp..."
echo "    VFS:  $PS3_VFS_ROOT"
echo "    Exec: $BIN"

exec "$BIN" "$@"
