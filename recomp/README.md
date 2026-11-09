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
  - The per-frame geometry batch job is replaced by a verified native implementation (byte-identical output across a 3,033-case equivalence harness), on by default — see *Performance* below and `notes/RECONSTRUCTION-050.md`.
- **Built-in Cheats menu**: a "Cheats" submenu in Options and in the pause menu (Thorium, Buildables, Stockpiles, base Health, and an invulnerable Ship), per-player in Battle mode via a Player selector. See *Cheats* below.
- **RSX Graphics & Vulkan Acceleration**:
  - Full Vulkan backend with native RSX NV3089 GPU blits and Wayland/X11 swapchain support.
  - Accelerated fragment program and vertex program execution (`PS3RECOMP_VK_GUEST_PROGRAMS=1`).
  - Native 1080p output: the present target and window follow the offered video mode.
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
- File Size: `2,817,600` bytes

### 2b. Assemble the VFS in order

The VFS the launcher uses is an overlay, applied in this order:

1. The full game package's `USRDIR` (base files).
2. The v1.10 patch package's `USRDIR` on top (it alone carries the `language*.utf` tables).
3. The Bionic Swarm DLC's `USRDIR/data` merged in (`data3.pak`-`data5.pak`, `dlc1.edat`, `models/expansion/`, levels 94/95/121-130/165-169). The expansion code ships in the base EBOOT; only its assets are in the DLC package (content ID `UP2009-NPUB30061_00-PELFASTCC00DLC00`). Full procedure and evidence: `notes/RECONSTRUCTION-037.md`.

Title-specific runtime fixes applied on top of upstream ps3recomp are catalogued in `recomp/RUNTIME_PATCHES.md` (AA blit gate, quit confirmation, 1080p present size, right-stick range boost).

### 3. Run the automated setup + build
`setup.sh` wraps the whole pipeline: it checks prerequisites,
initializes the submodule, runs `scripts/setup.py` (validates your
ELF, lifts all PPU functions into C++, extracts and lifts all 11
SPU workloads, generates the fingerprint dispatch table, applies
this project's lift patches — the Cheats menu and the SPU
geometry-job override — and assembles the VFS overlay in the
order of section 2b), then configures and builds:
```bash
cd recomp
./setup.sh --eboot /path/to/extracted/EBOOT.ELF \
           --gamedata /path/to/base-game-tree \
           --patchdata /path/to/patch-1.10-tree \
           --dlcdata /path/to/bionic-swarm-tree
```
`--patchdata` and `--dlcdata` are optional but recommended (the
patch tree carries the language tables; the DLC enables the
expansion content). Re-running `setup.sh` skips the lift when its
output already exists; pass `--force-lift` to regenerate it.

### 4. Launch the Game
Run the provided launcher script (it takes the ELF path):
```bash
./run.sh /path/to/extracted/EBOOT.ELF
```

---

## Controls

The recompilation supports both gamepads and keyboard controls out of the box.

### Gamepad (DualShock 4 / DualSense / Xbox)
| Gamepad Control | Action |
|:---|:---|
| **Left Analog Stick / D-Pad** | Fly the ship / Navigate menus |
| **Right Analog Stick** | Radial menu: push toward a unit type and **hold at the rim (~1 s)** to send units |
| **Cross (A)** | Select / Build / Confirm |
| **Circle (B)** | Cancel / Back |
| **Square (X)** | Sell Tower / Special |
| **Triangle (Y)** | Upgrade Tower / Unit Selection Menu |
| **L1 / R1** | Cycle unit types / Cycle weapons |
| **R2 (hold)** | Tractor beam — grab a flying rock (the game's help text: "Grab flying rock: Hold R2") |
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
| `PS3_VIDEO_RES` | `1080` | Output resolution mode (`720` or `1080`). The Vulkan present target and window are created at this mode's size (1920x1080 for `1080`); the game renders into matching display buffers when the mode is offered. `720` keeps the original 1280x720 path. |
| `VK_DRAW_BATCH` | `1` | Batches draw calls before presentation for higher frame rates and reduced CPU overhead. |
| `PS3RECOMP_VK_GUEST_PROGRAMS` | `1` | Enables GPU compilation of guest RSX fragment and vertex programs. |
| `PS3RECOMP_VK_WINDOW` | `1` | Creates an interactive Vulkan desktop window. |
| `MSGDIALOG_ANSWER` | `no` | Auto-answers system message dialogs (e.g. initial network/trophy prompts). |
| `PS3_VFS_ROOT` | `recomp/vfs` | Path to the virtual file system root containing `USRDIR/` and `PARAM.SFO`. |
| `PAD_NO_KEYBOARD` | `0` | Set to `1` to disable keyboard fallback mapping. |
| `PAD_STICK_BOOST` | `1.25` | Right-stick range boost in the pad layer, mimicking DualShock 3 rim saturation; titles with hard-tilt gates near full scale (Comet Crash's unit-send commit needs ~246+/255) are otherwise unreachable on modern pads. `1.0` disables. Wireless DualShock 4 owners who can't quite reach the send gate should try `1.5`. See `RUNTIME_PATCHES.md` entry 12. |
| `PS3_NO_JOB_OVERRIDE` | unset | Set to `1` to disable the native SPU geometry-job override and run the lifted job instead (debugging / A-B switch). The override is on by default. |
| `PAD_POLL_INTERVAL_MS` | `16` | Fresh pad-report cadence. `16` matches the hardware contract; the old `4` made pause menus auto-repeat ~4x too fast. |
| `AUDIO_WAV` | unset | When set to a file path, the final audio mix is recorded there (raw float32 stereo, 48 kHz) — useful for audio bug reports. Example: `AUDIO_WAV=/tmp/mix.wav ./run.sh /path/to/EBOOT.ELF`. |
| `AUDIO_MIX_NORM` | `0` | Set to `1` to scale would-clip mixed blocks to peak instead of hard-clipping, when many sounds stack at once. Opt-in pending wider listening tests. |
| `AUDIO_HEADROOM` | `0.5` | Fixed master-gain headroom applied to the finished mix before clipping (-6 dB at the default), so overlapping sounds stop slamming into the clip ceiling. `1.0` restores the old full-scale mix. |
| `AUDIO_CONCEAL` | `0` | Set to `1` to enable the mixer concealment stack: a bounded wait for late audio blocks, and waveform-matched concealment of blocks that never arrive, instead of silent holes (sub-knobs `AUDIO_ELASTIC_US`, `AUDIO_PLC_MODE`). |

| `AUDIO_PEAK` / `AUDIO_RATE` | unset | Audio diagnostics: when set, the cellAudio mixer periodically logs the mixed blocks' peak amplitude and the silent-block percentage. |

---

## Cheats

The build adds a **Cheats** submenu to the game's own Options menu
and to the pause menu (the shipped game had none). Toggles take
effect immediately and are player-side only:

| Toggle | Effect |
|:---|:---|
| **Thorium** | Pins your Thorium at the configured amount (default 1000). |
| **Buildables** | All structures buildable regardless of upgrade level. |
| **Stockpiles** | Pins every unit stockpile at the configured amount (default 60 per type). |
| **Health** | Your base's life stays at its per-match maximum. |
| **Ship** | Your ship is invulnerable (its health stays at max). |

In **Battle** mode (up to 4 players) a **Player: N** selector row
at the top of the submenu picks whose four toggles are shown and
flipped; each player's cheats are independent.

State persists in `~/.cc_cheats` (one `key=0|1` per line:
`thorium`, `buildables`, `stockpiles`, `health`, `ship`, plus
`p2_`…`p4_` prefixes for Battle players, and `thorium_amount=` /
`stockpile_amount=` for the amounts, clamped to 1000 / 60). The
file is re-read live when it changes. Environment seeds also work:
`CC_CHEAT_THORIUM=1`, `CC_CHEAT_SHIP=1`, … (`CC_CHEAT_P2_SHIP=1`
style for other players). Design and verification:
`notes/RECONSTRUCTION-041.md` and `notes/RECONSTRUCTION-049.md`.

---

## Performance

Defaults are tuned for play: draw batching (`VK_DRAW_BATCH=1`),
guest shader programs on the GPU, a fragment-program memo in the
draw path, and the native SPU geometry-job override described
above (roughly 2–3x faster per call than the lifted job on
transform-heavy scenes, byte-identical output). The game is
CPU-bound on the host in busy scenes; the remaining hotspot under
very heavy unit counts is the game's entity-simulation SPU job,
which is still the lifted code — see `notes/RECONSTRUCTION-050.md`
for the analysis and what a further override would require.

## Save states (experimental)

On the `muse/save-states` branch the recomp can snapshot a
match: pause the game and press **F5** to write
`~/tools/cc-recomp/saves/slot1.ccsave` (~110 MB, uncompressed);
**F9** loads it back (in a match, at the pause, or at the title
screen). Restores are byte-exact - the saved mission, mode,
units and resources come back precisely, and the mission id in
the snapshot is what the game's own progression code would
credit on a clear.

**Current limit, stated plainly:** after an in-game load the
restored match does not resume - the simulation stays frozen,
or the game leaves the match - because the restore cannot yet
stop and re-sync the runtime's own threads the way the
prototype's external tool could. The design, the test evidence
(including the one flow that does resume today) and the known
issues are in `notes/051-save-states.md`. Treat F5/F9 as an
experimental snapshot mechanism, not a finished save feature.

## Known issues

- **Audio character**: music and SFX play through the native host
  MP3 codec (no firmware). A few sounds are still under
  investigation for timbre — notably the held tractor-beam loop,
  which can sound chopped. If stacked sounds clip harshly, try
  `AUDIO_MIX_NORM=1`.
- **Extreme cheat-fed densities**: pushing unit counts far beyond
  the game's supported totals (easy with the Stockpiles cheat) can
  corrupt the scene geometry. This is believed to be the game's
  own buffer limits, not a porting bug; it does not occur at
  normal unit counts.
- **First-upgrade color shift**: the first structure upgrade of a
  session can render with a purple/green shift; it clears on the
  next upgrade tier. Under investigation.
- **Ship render blip**: a rare one-frame ship displacement has
  been observed; under investigation.

---

## Architecture & Technical Notes

1. **SPURS Workload Dispatch**:
   Comet Crash utilizes the Sony SPURS (SPU Runtime System) task queue extensively for pathfinding, collision detection, and swarm AI. This recompilation extracts all 9 embedded job binaries from the `.data` segment of `EBOOT.ELF` and compiles them as native C workloads, invoking them directly on the host CPU thread pool with hardware SSE/AVX vector intrinsics.

2. **Resolution & Viewport Alignment**:
   Comet Crash never calls cellResc. It renders into display buffers at the resolution the video-out configuration offers: 1280x720, or 1920x1080 when `PS3_VIDEO_RES=1080` (the launcher default). The recomp's Vulkan present target and window are created at that same size -- see `RUNTIME_PATCHES.md` entry 11 for why the present size must follow the mode.

3. **Audio & File IO**:
   CellFS calls are mapped directly to the local directory structure. All of the game's audio is MP3, decoded on the console by Sony's MultiStream SPU task using a codec plugin from the system firmware. This build replaces that plugin with a native host decoder (minimp3) answering at the same seam, so **no PlayStation firmware is needed** — see `notes/RECONSTRUCTION-039.md` and `notes/RECONSTRUCTION-040.md`. Output runs through the HLE `cellAudio` mixer.

---

## Credits

- **Comet Crash** developed by [Pyticus Studios](https://www.pyt-games.com/) / Sony Computer Entertainment.
- **ps3recomp** static recompilation framework by [sp00nznet](https://github.com/sp00nznet/ps3recomp).
- Static recompilation port and toolchain enhancements by [gcerkez](https://github.com/gcerkez).
