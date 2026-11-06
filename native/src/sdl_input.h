// sdl_input.h — SDL3 backend for the unified input model.
//
// Reads the three physical sources the native host supports —
//   * keyboard (arrows/WASD + action keys; see the mapping in the .cpp),
//   * mouse (absolute cursor, buttons, wheel),
//   * any SDL-supported gamepad, with hot-plug (DualShock/DualSense,
//     Xbox, Switch Pro, ... all arrive through SDL's gamepad API),
// and fills the SDL-free RawInput struct from input_types.h. This is the
// native replacement for the PS3 original's sys_io (ioPad*) sampling.

#pragma once

#include "input_types.h"

struct SDL_Gamepad;

namespace comet::input {

class SdlInput {
public:
    // Call after SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD).
    void beginFrame(); // drains the SDL event queue; call once per frame
    RawInput sample() const;

    bool quitRequested() const { return quit_; }
    bool gamepadConnected() const { return gamepad_ != nullptr; }

private:
    void openGamepad(unsigned int instanceId);
    void closeGamepad(unsigned int instanceId);

    SDL_Gamepad* gamepad_ = nullptr;
    unsigned int gamepadId_ = 0;

    bool quit_ = false;
    bool mousePresent_ = false;
    bool mouseMoved_ = false;
    float mouseX_ = 0.0f, mouseY_ = 0.0f;
    int wheelAccum_ = 0;
};

} // namespace comet::input
