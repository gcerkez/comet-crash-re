#!/bin/bash
# Comet Crash (NPUB30061) - Static Recompilation Launcher
#
# Runs build/CometCrashRecomp (from ./setup.sh) with the full performance stack:
#   - RSX Vulkan GPU Compute Offload (vertex decode, index expansion, depth/color resolve)
#   - Vulkan guest fragment/vertex program compilation + draw call batching
#   - SPU Float Flush helpers eliminating slow software fallback loops
#   - SPU Job 187280 (dragon/entity mesh 3D transform & culling optimizations)
#   - SPU Job 187F00 (sprite depth sort with AVX2 hardware FMA)
#   - SPU Job 188300 (combat target scan loop with candidate filter rejection)
#   - Native 1080p present sizing, point sprites, and pad cadence tuning
#
# Usage:
#   ./run.sh [/path/to/EBOOT.ELF] [options]
#
# Performance / Diagnostic Switches (pass before ./run.sh):
#   GPU Offload Switches:
#     PS3RECOMP_VK_GPU_VERTEX=0   Disable GPU compute vertex decode (use CPU unpack)
#     PS3RECOMP_VK_DEPTH_COLOR=0  Disable GPU compute depth/color resolve
#     PS3RECOMP_VK_COALESCE_RP=0  Disable render pass coalescing
#     PS3RECOMP_VK_DYN_DESC=0     Disable dynamic descriptor updates
#     PS3RECOMP_VK_ASYNC_SUBMIT=1 Enable asynchronous queue submission
#     RSX_PRIM_STATS=1            Log FPS and draw counts per primitive type once/sec
#     VK_BATCH_STATS=1            Log Vulkan draw batching hit/miss statistics
#
#   SPU Job Switches:
#     PS3_NO_JOB_OVERRIDE=1       Disable all native SPU job overrides (run lifted SPU)
#     PS3_NO_JOB187F00=1          Disable SPU Job 187F00 override only
#     CC_JOB188300_SCAN=0         Disable Job 188300 target scan native loop
#     J187280_TWOPHASE=0          Job 187280: run serial per-group path
#     J187280_DIRTY_ALL=1         Job 187280: mark all 256KB local store dirty
#     J187280_STATS=1             Job 187280: log group and timing statistics
#
#   Bit-Exact Verification Modes:
#     CC_JOB188300_VERIFY=1       Verify Job 188300 against lifted code (all 128 regs)
#     CC_JOB187F00_VERIFY=1       Verify Job 187F00 against lifted code (full 256KB LS)
#     J187280_VERIFY=1            Verify Job 187280 against lifted code (full 256KB LS)
#
#   Configuration & System:
#     CC_BUILD=<dir>              Use a specific build folder (default: build, or build-spuperf)
#     PS3_VIDEO_RES=720|1080      Display resolution (default: 1080)
#     PAD_STICK_BOOST=1.25        Right stick deadzone/rim boost for DS3/DS4/Xbox
#     MSGDIALOG_ANSWER=no         Auto-answer dialogs (bypasses PSN retry loop)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Resolve binary
BIN=""
for b in "${CC_BUILD:-}" "build" "build-spuperf" "build-ship" "build-batch"; do
    if [ -n "$b" ] && [ -x "$SCRIPT_DIR/$b/CometCrashRecomp" ]; then
        BIN="$SCRIPT_DIR/$b/CometCrashRecomp"
        break
    fi
done

if [ -z "$BIN" ]; then
    echo "[!] Executable not found in $SCRIPT_DIR/build or $SCRIPT_DIR/build-spuperf." >&2
    echo "    Please configure and build the project first:" >&2
    echo "      ./setup.sh --eboot /path/to/EBOOT.ELF --gamedata /path/to/gamedata" >&2
    exit 1
fi

# Resolve EBOOT.ELF
EBOOT=""
ARGS=()
for arg in "$@"; do
    if [ -z "$EBOOT" ] && [[ "$arg" == *.ELF || "$arg" == *.elf ]]; then
        EBOOT="$arg"
    else
        ARGS+=("$arg")
    fi
done

if [ -z "$EBOOT" ]; then
    if [ -n "${PS3_EBOOT:-}" ] && [ -f "$PS3_EBOOT" ]; then
        EBOOT="$PS3_EBOOT"
    elif [ -f "$SCRIPT_DIR/EBOOT.ELF" ]; then
        EBOOT="$SCRIPT_DIR/EBOOT.ELF"
    elif [ -f "$HOME/Downloads/comet_crash_extracted/EBOOT.ELF" ]; then
        EBOOT="$HOME/Downloads/comet_crash_extracted/EBOOT.ELF"
    fi
fi

if [ -z "$EBOOT" ]; then
    echo "Usage: $0 /path/to/EBOOT.ELF [extra arguments]" >&2
    echo "       Or set PS3_EBOOT=/path/to/EBOOT.ELF" >&2
    exit 1
fi

# Core defaults
export PS3_VFS_ROOT="${PS3_VFS_ROOT:-$SCRIPT_DIR/vfs}"
export PS3_TITLE="Comet Crash (ps3recomp)"
export PS3RECOMP_VK_WINDOW="${PS3RECOMP_VK_WINDOW:-1}"
export PS3RECOMP_VK_GUEST_PROGRAMS="${PS3RECOMP_VK_GUEST_PROGRAMS:-1}"
export PS3RECOMP_SPIRV_CACHE="${PS3RECOMP_SPIRV_CACHE:-/tmp/spirv_cache}"
export VK_DRAW_BATCH="${VK_DRAW_BATCH:-1}"
export PS3_VIDEO_RES="${PS3_VIDEO_RES:-1080}"
export PAD_STICK_BOOST="${PAD_STICK_BOOST:-1.25}"
export MSGDIALOG_ANSWER="${MSGDIALOG_ANSWER:-no}"

# GPU compute & optimization defaults
export PS3RECOMP_VK_GPU_VERTEX="${PS3RECOMP_VK_GPU_VERTEX:-1}"
export PS3RECOMP_VK_DEPTH_COLOR="${PS3RECOMP_VK_DEPTH_COLOR:-1}"
export PS3RECOMP_VK_COALESCE_RP="${PS3RECOMP_VK_COALESCE_RP:-1}"
export PS3RECOMP_VK_DYN_DESC="${PS3RECOMP_VK_DYN_DESC:-1}"
export CC_JOB188300_SCAN="${CC_JOB188300_SCAN:-1}"

# Diagnostic & debug pass-throughs
export PAD_DEBUG_LOG="${PAD_DEBUG_LOG:-0}"
export CC_CAM_DIAG="${CC_CAM_DIAG:-0}"
export CC_LOCK_ZOOM="${CC_LOCK_ZOOM:-0}"
export CC_FORCE_OVERVIEW="${CC_FORCE_OVERVIEW:-0}"
export CC_FORCE_FOLLOW="${CC_FORCE_FOLLOW:-0}"
export PS3RECOMP_VP_DIAG="${PS3RECOMP_VP_DIAG:-0}"

echo "[*] Launching Comet Crash Recomp..."
echo "    Executable: $BIN"
echo "    EBOOT:      $EBOOT"
echo "    VFS:        $PS3_VFS_ROOT"

exec "$BIN" "$EBOOT" "${ARGS[@]}"
