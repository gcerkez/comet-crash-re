# Comet Crash — native host

The native C++ host the reconstruction grows into. Milestone 1 was the
shell: an SDL3 window and the **unified input layer** that replaces the
PS3 original's `sys_io` (cellPad*) sampling — keyboard and mouse stand
in for a controller, and a real gamepad works the moment it's plugged
in. **Milestone 2 adds the game's menus**: the original's screens, order
and strings, navigable end to end (title → main menu → campaign /
battle setup / settings / scores / credits → arena placeholder → pause
menu with its Yes/No confirmations).

See [notes/NATIVE-PORT-001.md](../notes/NATIVE-PORT-001.md) for the full
architecture, the PS3→native service map, and the roadmap.

## Layout

- `src/input_types.h` — the unified input model (`InputFrame` + raw
  per-source states). Button bits mirror the PS3 pad layout.
- `src/input_mapper.*` — merges keyboard/mouse/gamepad raw states into
  one `InputFrame` per player: edge detection, stick/digital movement,
  and the cursor (mouse-owned while it moves; integrated from stick/keys
  otherwise, like the original's pad cursor). SDL-free and unit-tested.
- `src/sdl_input.*` — the SDL3 backend: keyboard state, mouse, and
  hot-pluggable gamepads via SDL's gamepad API.
- `src/text_table.*` — the game's string table: `text(index)` returns
  the same strings the PS3 build shows, index-compatible with the
  original's `local_text` loader. Data is generated into
  `src/language1.inc` from the extracted table by
  `scripts/gen_text_table.py`.
- `src/menu_model.*` — the menu flow as a pure SDL-free state machine
  (screens, selection, sliders/toggles/cycles, pause + confirmations,
  the arena placeholder's state). Unit-tested.
- `src/main.cpp` — the frame loop and renderer (SDL debug-text menus;
  the arena screen shows the placeholder grid, cursor, and the current
  mission's briefing text).
- `tests/input_mapper_test.cpp` — headless checks for the merge logic.
- `tests/menu_model_test.cpp` — headless checks driving the full menu
  flow with synthetic input frames.

## Build

Needs a C++17 compiler, CMake ≥ 3.20, and SDL3 development files.

```bash
cmake -S native -B native/build
cmake --build native/build
ctest --test-dir native/build --output-on-failure
./native/build/comet_native
```

On JerryDesktop the host OS has the SDL3 runtime but no headers/toolchain
for this, so builds run in the existing Fedora 44 distrobox (which shares
the home directory):

```bash
distrobox enter iloader -- bash -lc '
  cd ~/comet-crash-re &&
  cmake -S native -B native/build &&
  cmake --build native/build -j &&
  ctest --test-dir native/build --output-on-failure'
```

Smoke test without a display (exits after 30 frames, logs input state):

```bash
SDL_VIDEODRIVER=dummy COMET_NATIVE_FRAMES=30 COMET_NATIVE_LOG=1 \
  ./native/build/comet_native
```

## Controls (player 1)

| Game action | Keyboard / mouse | Gamepad (SDL standard) |
|---|---|---|
| Move / cursor | Arrows or WASD; mouse position | Left stick, D-pad |
| Confirm (Cross) | Enter, Space, left mouse button | South button |
| Back (Circle) | Esc, Backspace, right mouse button | East button |
| Action (Triangle) | F, middle mouse button | North button |
| Alt (Square) | G | West button |
| Start | P | Start |
| Select | Tab | Back |
| L1 / R1 | Q / E | Shoulder buttons |
| L2 / R2 | 1 / 3, mouse wheel (wheel only) | Analog triggers |

Start+Back together quits the host; closing the window does too.

## Status / limits

- Player 1 only for now; the model extends to 4 players (the original is
  1–4P) by sampling one `RawInput` per pad.
- Menus are complete and faithful; gameplay is not there yet — entering
  a mode lands on the arena placeholder (grid + briefing + working
  pause menu). Host approximations are confined to value ranges the
  strings don't pin down (e.g. Max Hits 1–20, volume 0–10).
- Next milestones are in the port notes (boot walk and function naming
  on the reconstruction side; PSGL-shim renderer on the host side).
