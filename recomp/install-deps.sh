#!/bin/bash
# Comet Crash recompilation -- install build dependencies.
#
# Detects apt (Debian/Ubuntu), dnf (Fedora/Bazzite/RHEL) or pacman
# (Arch) and installs the system packages the build needs, then
# installs the ps3recomp Python tool requirements from the
# submodule (capstone, pycryptodome, construct, tabulate, tqdm).
#
# System packages: C/C++ toolchain, CMake, Ninja, pkg-config, git,
# SDL2 development files, Vulkan loader + headers + tools, glslang
# and SPIRV-Cross (the RSX Vulkan backend compiles guest shaders
# with them; the runtime also carries fallbacks).
#
# Usage:  ./install-deps.sh [--system-only | --python-only]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
REQ="$REPO_ROOT/third_party/ps3recomp/requirements.txt"

DO_SYSTEM=1
DO_PYTHON=1
for a in "$@"; do
    case "$a" in
        --system-only) DO_PYTHON=0 ;;
        --python-only) DO_SYSTEM=0 ;;
        *) echo "[!] unknown option: $a" >&2; exit 2 ;;
    esac
done

SUDO=""
if [ "$(id -u)" != 0 ]; then
    SUDO="sudo"
fi

if [ "$DO_SYSTEM" = 1 ]; then
    if command -v apt-get >/dev/null 2>&1; then
        echo "[*] apt-based system detected"
        $SUDO apt-get update
        $SUDO apt-get install -y \
            build-essential cmake ninja-build pkg-config git \
            libsdl2-dev libvulkan-dev vulkan-tools \
            glslang-tools spirv-tools python3 python3-pip
    elif command -v dnf >/dev/null 2>&1; then
        echo "[*] dnf-based system detected (Fedora/Bazzite/RHEL)"
        $SUDO dnf install -y \
            gcc gcc-c++ cmake ninja-build pkgconf-pkg-config git \
            SDL2-devel vulkan-loader-devel vulkan-headers vulkan-tools \
            glslang spirv-tools python3 python3-pip
    elif command -v pacman >/dev/null 2>&1; then
        echo "[*] pacman-based system detected (Arch)"
        $SUDO pacman -S --needed --noconfirm \
            base-devel cmake ninja pkgconf git \
            sdl2 vulkan-devel vulkan-tools \
            glslang spirv-cross python python-pip
    else
        echo "[!] No supported package manager found (apt-get, dnf, pacman)."
        echo "    Install equivalents of: gcc/g++, cmake, ninja, pkg-config,"
        echo "    SDL2 dev, Vulkan loader+headers dev, glslang, spirv-cross,"
        echo "    python3 + pip -- then re-run with --python-only."
        exit 1
    fi
fi

if [ "$DO_PYTHON" = 1 ]; then
    if [ ! -f "$REQ" ]; then
        echo "[*] Submodule not initialized yet; initializing..."
        git -C "$REPO_ROOT" submodule update --init --recursive
    fi
    echo "[*] Installing Python tool requirements from $REQ"
    python3 -m pip install --user -r "$REQ"
fi

echo "[+] Dependencies installed. Next: ./setup.sh --eboot ... (see README.md)"
