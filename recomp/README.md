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
- **RSX Graphics & Vulkan Acceleration**:
  - Full Vulkan backend with native RSX NV3089 GPU blits and Wayland/X11 swapchain support.
  - Accelerated fragment program and vertex program execution (`PS3RECOMP_VK_GUEST_PROGRAMS=1`).
  - Viewport & `cellResc` hardware resolution scaling support with pixel-perfect native 720p centering.
- **Input & Controllers**:
  - Full SDL2 GameController support (Sony DualShock 3/4, DualSense, Microsoft Xbox controllers).
  - Dual-motor rumble and haptic support.
  - Built-in keyboard fallback with seamless controller merging.
- **Virtual File System (VFS)**:
  - Transparent redirection of `/app_home/`, `/dev_hdd0/game/NPUB30061/`, and game resource packages.

---

## Prerequisites

### Linux (Ubuntu 22.04+ / Fedora 39+ / Arch / Bazzite)
```bash
# Ubuntu / Debian
sudo apt install build-essential cmake ninja-build libvulkan-dev vulkan-tools libsdl2-dev python3 python3-pip

# Fedora / Bazzite / RHEL
sudo dnf install gcc gcc-c++ cmake ninja-build vulkan-loader-devel vulkan-tools SDL2-devel python3

# Arch Linux
sudo pacman -S base-devel cmake ninja vulkan-devel sdl2 python
```

---

## Quick Start

### 1. Clone the Repository
Clone with recursive submodules to pull the required `ps3recomp` runtime:
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
- File Size: `2,108,304` bytes

### 3. Run Automated Setup & Lift Pipeline
The `setup.py` script validates your ELF, lifts all PPU functions into C++, extracts and lifts all 11 SPU workloads, generates workload fingerprint dispatchers, and links game data into the VFS:
```bash
python3 recomp/scripts/setup.py \
    --eboot /path/to/extracted/EBOOT.ELF \
    --game-dir /path/to/extracted/NPUB30061
```

### 4. Build the Native Binary
Compile the project with CMake and Ninja:
```bash
cd recomp
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 5. Launch the Game
Run the provided launcher script:
```bash
./run.sh
```

---

## Controls

The recompilation supports both gamepads and keyboard controls out of the box.

### Gamepad (DualShock 4 / DualSense / Xbox)
| Gamepad Control | Action |
|:---|:---|
| **Left Analog Stick / D-Pad** | Move cursor / Navigate menus / Steer unit |
| **Cross (A)** | Select / Build / Confirm |
| **Circle (B)** | Cancel / Back |
| **Square (X)** | Sell Tower / Special |
| **Triangle (Y)** | Upgrade Tower / Unit Selection Menu |
| **L1 / R1** | Cycle unit types / Cycle weapons |
| **L2 / R2** | Fast scroll / Camera zoom |
| **Start** | Pause / In-Game Menu |
| **Select** | Game Stats / Overview |

### Keyboard Fallback
When no gamepad is connected (or alongside a connected controller), keyboard input is automatically mapped:
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

The launcher script sets optimal defaults, but you can customize runtime behavior using environment variables:

| Variable | Default | Description |
|:---|:---|:---|
| `PS3_VIDEO_RES` | `720` | Output resolution mode (`720` or `1080`). Setting to `720` provides pixel-perfect 1:1 match to the game's internal 3D scene engine. |
| `VK_DRAW_BATCH` | `1` | Batches draw calls before presentation for higher frame rates and reduced CPU overhead. |
| `PS3RECOMP_VK_GUEST_PROGRAMS` | `1` | Enables GPU compilation of guest RSX fragment and vertex programs. |
| `PS3RECOMP_VK_WINDOW` | `1` | Creates an interactive Vulkan desktop window. |
| `MSGDIALOG_ANSWER` | `no` | Auto-answers system message dialogs (e.g. initial network/trophy prompts). |
| `PS3_VFS_ROOT` | `recomp/vfs` | Path to the virtual file system root containing `USRDIR/` and `PARAM.SFO`. |
| `PAD_NO_KEYBOARD` | `0` | Set to `1` to disable keyboard fallback mapping. |

---

## Architecture & Technical Notes

1. **SPURS Workload Dispatch**:
   Comet Crash utilizes the Sony SPURS (SPU Runtime System) task queue extensively for pathfinding, collision detection, and swarm AI. This recompilation extracts all 9 embedded job binaries from the `.data` segment of `EBOOT.ELF` and compiles them as native C workloads, invoking them directly on the host CPU thread pool with hardware SSE/AVX vector intrinsics.

2. **Resolution Scaling (`cellResc`) & Viewport Alignment**:
   Comet Crash's 3D playfield renders to a $1280 \times 720$ offscreen buffer, while querying `cellVideoOutGetResolutionAvailability`. Setting `PS3_VIDEO_RES=720` configures the display pipeline to run at native 720p, eliminating letterbox squishing and ensuring the entire 3D arena fills the display window.

3. **Audio & File IO**:
   CellFS calls are mapped directly to the local directory structure. Audio streams (`.at3`, `.vag`, and BGM) are serviced through the HLE `cellAudio` and `cellAtrac` modules.

---

## Credits

- **Comet Crash** developed by [Pyticus Studios](https://www.pyt-games.com/) / Sony Computer Entertainment.
- **ps3recomp** static recompilation framework by [sp00nznet](https://github.com/sp00nznet/ps3recomp).
- Static recompilation port and toolchain enhancements by [gcerkez](https://github.com/gcerkez).
