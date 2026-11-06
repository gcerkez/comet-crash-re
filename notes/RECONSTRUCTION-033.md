# Reconstruction notes 033 — Title screen & live game loop reached via ps3recomp

Date: 2026-11-06.

Following the initial bring-up in notes 032, the remaining startup blockers were resolved across the runtime and HLE layers:

1. **`cellPngDecOpen`**: Validates and loads source data upfront; returns `CELL_PNGDEC_ERROR_OPEN_FILE` (`0x80611151`) on missing assets, preventing an uninitialized stack branch in `FUN_0016eb54`.
2. **Unified File Descriptors**: `cellFsSdataOpen` in `ppu_fs.cpp` routes through `sys_fs.c`'s shared descriptor table, allowing `package.lua.sdat` to be opened and read without `EBADF`.
3. **`sceNpDrmIsAvailable2`**: Verifies existence of DRM files (`dlc1.edat`) with `fopen`. Returns `0x80029513` when DLC is absent instead of incorrectly reporting `CELL_OK`, preventing the game from aborting when optional DLC asset packs (`data3.pak`, etc.) are missing.
4. **SPURS Urgent Commands & Joblists**: Implemented `cellSpursAddUrgentCommand` (`0x17001000`), `cellSpursAddUrgentCall` (`0x8FDF50B2`), and opcode 6 (`JOBLIST`) in `cellSpurs.c`. When the main thread enqueues completion events, the job chain dispatches all constituent tasks and unblocks waiting threads on `cellSpursEventFlagWait`.
5. **Headless MsgDialog Network Loop**: Comet Crash opens a yes/no dialog when offline:
   ```
   Not connected to PlayStation®Network. Try again?
   Choosing no will disable scoreboard features.
   ```
   With the default auto-responder answering `YES`, the title repeatedly attempted network auto-connection and re-prompted in a tight loop. Setting `MSGDIALOG_ANSWER=no` answers `NO`, disabling online leaderboards and allowing the title to advance.

## Boot Verification

With `MSGDIALOG_ANSWER=no PS3_VFS_ROOT=$HOME/tools/cc-recomp/vfs ./build/CometCrashRecomp`:
- Shaders loaded and compiled: `font_rgba_shader`, `star_shader`, `particle_texture_shader`, `texture_shader`, `down_sample`, `shadows`, `bloom2light`, `hdr_glow_x/y`, `hdr_tone_map`, `lit_texture_shader`, `lit_object_shader`, `lit_texture_spec_gloss_shader`.
- Models & textures streamed from `data1.pak`, `data2.pak`, and loose files: `playerShip.obj`, `playerShipDonut.obj`, `sun.obj`, `converter02.obj`, `stunner02.obj`, and advanced agent meshes (`agentAdv_Medic.obj`, `agentAdv_Carrier.obj`, `agentAdv_Laser.obj`).
- Audio mixing thread active (`cellAudio` mixing port 0, SPU MultiStream taskset active).
- Game loop successfully executing at 60 Hz (8,400+ flips verified).
