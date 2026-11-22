# Comet Crash (NPUB30061) - Static Recompilation

A clean-room, high-performance static recompilation of **Comet Crash** (PSN Title ID: `NPUB30061`) for 64-bit PC platforms (Linux x86_64 and Windows x64) built on the [ps3recomp](https://github.com/sp00nznet/ps3recomp) framework.

---

## Legal & Clean-Room Notice

This repository contains **strictly zero proprietary code, encryption keys, copyrighted game assets, or dumped binaries**. 

To build and run this static recompilation, you **must provide your own legally purchased copy** of Comet Crash dumped from a PlayStation 3 console. The automated setup pipeline decrypts nothing and distributes nothing—it parses the user-supplied `EBOOT.ELF` and game data locally on your system.

---

## Features

- **PPU Static Recompilation**: Cell Broadband Engine PowerPC 64-bit instructions translated to native optimized C++ and compiled to native host machine code.
- **Dedicated SPU Execution Engine**:
  - 2 embedded SPU ELFs (`spu_0000_at_00175D00` and `spu_0001_at_001C0B80`) extracted and lifted to native C.
  - 9 raw SPURS job binaries lifted with automated fingerprint-based workload registration.
  - Multi-threaded parallel SPU worker pool (`spu_dispatch_mt`) with zero-allocation thread-local job staging.
  - **Hardware SIMD & AVX2 Acceleration**: Single-precision SPU float helpers vectorized with lane-exact AVX2 flush logic (`spu__x8_flush` / `spu__x4_flush`), eliminating software fallback stalls on unused/denormal register lanes.
  - **Instanced Mesh 3D Transform & Culling (Job 187280)**: Native vectorized 3D vertex transform (`j_xform_elem_fast`), hoisted group scale factors, hoisted 2 KB triangle culling state copies, and thread-local scratch buffers (verified byte-identical across a 3,033-case test harness and 80,000+ live game calls).
  - **Sprite Depth Sorting (Job 187F00)**: AVX2 hardware FMA vectorization in Phase 1 and single-cycle integer sort keys (`j_depth_key`) in Phase 2 capped gnome sort (verified bit-exact over 120,000+ calls).
  - **Combat Target Scan (Job 188300)**: Native scan loop with candidate filter rejection and lazy invariant hoisting, reducing scan time by over 80% (verified over 1.6M calls).
- **RSX Graphics & Vulkan Acceleration**:
  - Full Vulkan backend with native RSX NV3089 GPU blits and Wayland/X11 swapchain support.
  - **GPU Compute Offload Pipelines**:
    - Vertex attribute decode/unpack offloaded to GPU compute shaders (`rsx_vk_vertex_decode.comp`).
    - Index buffer expansion offloaded to GPU compute shaders (`rsx_vk_index_expand.comp`).
    - On-GPU texture decode and format conversion (`rsx_vk_tex_decode.comp`).
    - GPU compute depth/color resolve (`rsx_vk_depth_color.comp`).
  - Accelerated fragment and vertex program execution (`PS3RECOMP_VK_GUEST_PROGRAMS=1`).
  - Native 1080p output: the present target and window follow the offered video mode.
  - Ground-shadow point sprites with correct G8B8 atlas byte ordering.
- **Built-in Cheats Menu**: A "Cheats" submenu in Options and in the pause menu (Thorium, Buildables, Stockpiles, base Health, and invulnerable Ship), with independent per-player toggles in Battle mode.
- **Input & Controllers**:
  - Full SDL2 GameController support (Sony DualShock 3/4, DualSense, Microsoft Xbox controllers).
  - Dual-motor rumble and haptic feedback.
  - Configurable right-stick range boost (`PAD_STICK_BOOST=1.25`) for reliable unit-send rim saturation on modern gamepads.
  - Built-in keyboard fallback with seamless controller merging.
- **Virtual File System (VFS)**:
  - Transparent redirection of `/app_home/`, `/dev_hdd0/game/NPUB30061/`, and game resource packages.
  - MultiStream SPU audio codec replaced by clean-room native host MP3 decoder (minimp3) with zero firmware requirements.

---

## Prerequisites

### Linux (Ubuntu 22.04+ / Fedora 39+ / Arch / Bazzite / SteamOS)

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config git \
    libsdl2-dev libvulkan-dev vulkan-tools glslang-tools spirv-tools python3 python3-pip

# Fedora / Bazzite / RHEL
sudo dnf install gcc gcc-c++ cmake ninja-build pkgconf-pkg-config git \
    SDL2-devel vulkan-loader-devel vulkan-headers vulkan-tools glslang spirv-tools python3 python3-pip

# Arch Linux
sudo pacman -S --needed base-devel cmake ninja pkgconf git \
    sdl2 vulkan-devel vulkan-tools glslang spirv-cross python python-pip
```

---

## Quick Start

### 1. Clone the Repository
Clone with recursive submodules to pull the runtime fork:
```bash
git clone --recursive https://github.com/gcerkez/comet-crash-re.git
cd comet-crash-re
```

If already cloned without `--recursive`:
```bash
git submodule update --init --recursive
```

### 2. Prepare Game Files
Obtain your legally dumped, decrypted `EBOOT.ELF` and game folder from your PlayStation 3 console:
- Title ID: `NPUB30061`
- `EBOOT.ELF` SHA-256: `a18d8e4a4a2620fd58f7c09e9639f3de91b3197cab3877c554e369dee430a00c`
- File Size: `2,817,600` bytes

### 2b. Assemble the VFS in Order
The VFS used by the launcher is an overlay, applied in this sequence:
1. Full game package `USRDIR` (base assets).
2. v1.10 patch package `USRDIR` (carries the `language*.utf` text tables).
3. (Optional) Bionic Swarm DLC `USRDIR/data` (`data3.pak`-`data5.pak`, `dlc1.edat`, expansion levels).

### 3. Run the Automated Setup + Build
`setup.sh` handles prerequisites, submodule verification, lifting the PPU/SPU code, applying project patches, generating the SPU target scan loop, and building the binary:
```bash
cd recomp
./setup.sh --eboot /path/to/extracted/EBOOT.ELF \
           --gamedata /path/to/base-game-tree \
           --patchdata /path/to/patch-1.10-tree \
           --dlcdata /path/to/bionic-swarm-tree
```

To install package manager dependencies automatically before setup, pass `--deps`:
```bash
./setup.sh --deps --eboot /path/to/EBOOT.ELF --gamedata /path/to/base-game-tree
```

### 4. Launch the Game
Run the launcher script:
```bash
./run.sh /path/to/extracted/EBOOT.ELF
```

---

## Controls

The recompilation supports both gamepads and keyboard fallback controls out of the box.

### Gamepad (DualShock 4 / DualSense / Xbox)
| Gamepad Control | Action |
|:---|:---|
| **Left Analog Stick / D-Pad** | Fly the ship / Navigate menus |
| **Right Analog Stick** | Radial menu: push toward a unit type and **hold at rim (~1 s)** to send units |
| **Cross (A)** | Select / Build / Confirm |
| **Circle (B)** | Cancel / Back |
| **Square (X)** | Sell Tower / Special |
| **Triangle (Y)** | Upgrade Tower / Unit Selection Menu |
| **L1 / R1** | Cycle unit types / Cycle weapons |
| **R2 (hold)** | Tractor beam — grab a flying rock |
| **Start** | Pause / In-Game Menu |
| **Select** | Game Stats / Overview |

### Keyboard Fallback
| Key | Mapped Button |
|:---|:---|
| **Arrow Keys (Up/Down/Left/Right)** | Left Analog / D-Pad |
| **Z** | Cross (Build / Select) |
| **X** | Circle (Cancel / Back) |
| **A** | Square (Sell) |
| **S** | Triangle (Upgrade) |
| **Q / W** | L1 / R1 |
| **1 / 2** | L2 / R2 |
| **Enter (Return)** | Start |
| **Tab** | Select |

---

## Configuration & Environment Variables

| Variable | Default | Description |
|:---|:---|:---|
| **Display & RSX** | | |
| `PS3_VIDEO_RES` | `1080` | Output resolution mode (`720` or `1080`). Present target and window adapt to this resolution. |
| `VK_DRAW_BATCH` | `1` | Batches compatible draw calls before presentation for high frame rates and lower CPU overhead. |
| `PS3RECOMP_VK_GUEST_PROGRAMS`| `1` | Compiles guest RSX vertex and fragment programs directly to GPU SPIR-V shaders. |
| `PS3RECOMP_VK_WINDOW` | `1` | Creates an interactive Vulkan desktop window. |
| **GPU Compute Offload** | | |
| `PS3RECOMP_VK_GPU_VERTEX` | `1` | Offloads RSX vertex attribute decode and normalization to Vulkan compute shaders. `0` uses CPU unpack. |
| `PS3RECOMP_VK_DEPTH_COLOR` | `1` | Offloads depth and color surface resolves to Vulkan compute shaders. |
| `PS3RECOMP_VK_COALESCE_RP` | `1` | Coalesces compatible adjacent render passes into unified passes. |
| `PS3RECOMP_VK_DYN_DESC` | `1` | Uses dynamic descriptor indexing for texture and uniform updates. |
| `PS3RECOMP_VK_ASYNC_SUBMIT` | `0` | Asynchronous queue submission for transfer/compute workloads. |
| **SPU Optimizations** | | |
| `CC_JOB188300_SCAN` | `1` | Native scan loop for Job 188300 with candidate filter rejection and lazy hoisting. `0` runs translated lift. |
| `J187280_TWOPHASE` | `1` | Exact two-phase group parallel path for Job 187280 geometry job. `0` runs serial loop. |
| `J187280_WORKERS` | `4` | Worker thread count for Job 187280 pool. |
| `J187280_DIRTY_ALL` | `0` | `1` forces restaging all 256 KB of local store instead of exact dirty pages. |
| `PS3_NO_JOB_OVERRIDE` | `0` | Emergency kill switch: `1` disables all native SPU job overrides and falls back to lifted SPU code. |
| `PS3_NO_JOB187F00` | `0` | `1` disables only Job 187F00 depth sort override. |
| **Verification & Diagnostics** | | |
| `CC_JOB188300_VERIFY` | `0` | `1` runs native Job 188300 on register copies and compares all 128 registers against the lift at loop exit. |
| `CC_JOB187F00_VERIFY` | `0` | `1` runs native Job 187F00 on private copies and verifies all 256 KB of local store against the lift. |
| `J187280_VERIFY` | `0` | `1` verifies all 256 KB of local store for Job 187280 against the lifted job. |
| `RSX_PRIM_STATS` | `0` | `1` logs FPS and draw counts per primitive type once every second. |
| `VK_BATCH_STATS` | `0` | `1` logs Vulkan draw batch hits, flushes, and pipeline stats. |
| `J187280_STATS` | `0` | `1` logs Job 187280 call frequency, group sizes, and timing. |
| **Input & Audio** | | |
| `PAD_STICK_BOOST` | `1.25` | Right stick range boost for unit-send rim saturation. Set `1.5` for worn sticks; `1.0` disables. |
| `MSGDIALOG_ANSWER` | `no` | Auto-answers dialogs (prevents PSN connection retry loop). |
| `PAD_NO_KEYBOARD` | `0` | `1` disables keyboard fallback. |
| `AUDIO_HEADROOM` | `0.5` | Master headroom (-6 dB) to prevent clipping when numerous sounds stack. |

---

## Developing & Verifying on a New Machine

### 1. Build Verification
To build the recompilation manually:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPS3RECOMP_DIR=../third_party/ps3recomp -DCC_HOST_AVX2=ON
cmake --build build -j$(nproc)
```

### 2. Running Unit & Equivalence Tests
The runtime fork includes standalone test suites for the SPU helpers and GPU offload pipelines:
```bash
# In third_party/ps3recomp build directory:
cmake -S third_party/ps3recomp -B third_party/ps3recomp/build -G Ninja -DPS3RECOMP_BUILD_TESTS=ON
cmake --build third_party/ps3recomp/build --target test_spu_helpers test_vulkan_gpu_offload
./third_party/ps3recomp/build/tests/test_spu_helpers
./third_party/ps3recomp/build/tests/test_vulkan_gpu_offload
```

### 3. In-Game Bit-Exact Verification
Developers modifying SPU math or job implementations can run with live verification enabled:
```bash
# Verify Job 188300 (combat scan), Job 187F00 (depth sort), and Job 187280 (dragon transform):
CC_JOB188300_VERIFY=1 CC_JOB187F00_VERIFY=1 J187280_VERIFY=1 ./run.sh /path/to/EBOOT.ELF
```
Every dispatch will run both the native implementation and the lifted reference code in parallel and report any mismatch.

---

## Cheats

The build adds a **Cheats** submenu to the game's Options menu and pause menu:

| Toggle | Effect |
|:---|:---|
| **Thorium** | Pins Thorium at 1,000. |
| **Buildables** | All structures buildable regardless of upgrade tier. |
| **Stockpiles** | Pins unit stockpiles at 60 per type. |
| **Health** | Base life stays at maximum. |
| **Ship** | Ship is invulnerable. |

In **Battle mode**, a **Player: N** selector allows independent cheat configurations per player. Settings persist across sessions in `~/.cc_cheats`.

---

## Credits

- **Comet Crash** developed by [Pelfast](https://www.pelfast.com/) / Sony Computer Entertainment.
- **ps3recomp** static recompilation framework by [sp00nznet](https://github.com/sp00nznet/ps3recomp).
- Static recompilation port, GPU compute offload, and SPU optimizations by [gcerkez](https://github.com/gcerkez).
