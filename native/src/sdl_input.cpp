// sdl_input.cpp — SDL3 event/state polling. Keyboard mapping (player 1):
//
//   Move        Arrow keys or WASD        Confirm (Cross)   Enter / Space
//   Back (O)    Esc / Backspace           Action (Tri.)     F
//   Alt (Sqr.)  G                         Start             P
//   Select      Tab                       L1 / R1           Q / E
//   L2 / R2     1 / 3                     Quit host         window close,
//                                                           or Start+Back
//
// Mouse: position drives the cursor, left = Confirm, right = Back,
// middle = Action, wheel passes through. Gamepad: SDL's standard
// mapping (South = Cross, East = Circle, West = Square, North =
// Triangle, shoulders, triggers, both sticks, D-pad).

#include "sdl_input.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace comet::input {
namespace {

float normalizeAxis(Sint16 v) {
    float f = static_cast<float>(v) / 32767.0f;
    const float kDeadzone = 0.18f;
    if (std::fabs(f) < kDeadzone) return 0.0f;
    return f;
}

float normalizeTrigger(Sint16 v) {
    float f = static_cast<float>(v) / 32767.0f;
    return f < 0.0f ? 0.0f : f;
}

} // namespace

void SdlInput::openGamepad(unsigned int instanceId) {
    if (gamepad_ != nullptr) return; // milestone 1: player 1 only
    gamepad_ = SDL_OpenGamepad(instanceId);
    if (gamepad_ != nullptr) gamepadId_ = instanceId;
}

void SdlInput::closeGamepad(unsigned int instanceId) {
    if (gamepad_ != nullptr && instanceId == gamepadId_) {
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
        gamepadId_ = 0;
    }
}

void SdlInput::beginFrame() {
    mouseMoved_ = false;
    wheelAccum_ = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_EVENT_QUIT:
                quit_ = true;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mousePresent_ = true;
                mouseMoved_ = true;
                mouseX_ = ev.motion.x;
                mouseY_ = ev.motion.y;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                mousePresent_ = true;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                mousePresent_ = true;
                wheelAccum_ += static_cast<int>(ev.wheel.y);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                openGamepad(ev.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                closeGamepad(ev.gdevice.which);
                break;
            default:
                break;
        }
    }
    // Pick up a pad that was already plugged in before we started.
    if (gamepad_ == nullptr) {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids != nullptr) {
            for (int i = 0; i < count; ++i) {
                openGamepad(static_cast<unsigned int>(ids[i]));
                if (gamepad_ != nullptr) break;
            }
            SDL_free(ids);
        }
    }
}

RawInput SdlInput::sample() const {
    RawInput raw;

    // --- keyboard (polled state, so held keys just work)
    const bool* keys = SDL_GetKeyboardState(nullptr);
    auto down = [&](SDL_Scancode sc) { return keys != nullptr && keys[sc]; };
    KeyboardState& kb = raw.keyboard;
    kb.up = down(SDL_SCANCODE_UP) || down(SDL_SCANCODE_W);
    kb.down = down(SDL_SCANCODE_DOWN) || down(SDL_SCANCODE_S);
    kb.left = down(SDL_SCANCODE_LEFT) || down(SDL_SCANCODE_A);
    kb.right = down(SDL_SCANCODE_RIGHT) || down(SDL_SCANCODE_D);
    kb.confirm = down(SDL_SCANCODE_RETURN) || down(SDL_SCANCODE_SPACE) ||
                 down(SDL_SCANCODE_KP_ENTER);
    kb.back = down(SDL_SCANCODE_ESCAPE) || down(SDL_SCANCODE_BACKSPACE);
    kb.action = down(SDL_SCANCODE_F);
    kb.alt = down(SDL_SCANCODE_G);
    kb.start = down(SDL_SCANCODE_P);
    kb.select = down(SDL_SCANCODE_TAB);
    kb.l1 = down(SDL_SCANCODE_Q);
    kb.r1 = down(SDL_SCANCODE_E);
    kb.l2 = down(SDL_SCANCODE_1);
    kb.r2 = down(SDL_SCANCODE_3);

    // --- mouse
    MouseState& ms = raw.mouse;
    ms.present = mousePresent_;
    ms.moved = mouseMoved_;
    float mx = mouseX_, my = mouseY_;
    const Uint32 mask = SDL_GetMouseState(&mx, &my);
    if (mousePresent_ && !mouseMoved_) {
        // Keep the tracked position fresh even without motion events.
        ms.x = mx;
        ms.y = my;
    } else {
        ms.x = mouseX_;
        ms.y = mouseY_;
    }
    ms.left = (mask & SDL_BUTTON_LMASK) != 0;
    ms.right = (mask & SDL_BUTTON_RMASK) != 0;
    ms.middle = (mask & SDL_BUTTON_MMASK) != 0;
    ms.wheel = wheelAccum_;

    // --- gamepad (first connected pad drives player 1 for now)
    GamepadState& pad = raw.gamepad;
    if (gamepad_ != nullptr) {
        pad.connected = true;
        auto btn = [&](SDL_GamepadButton b) {
            return SDL_GetGamepadButton(gamepad_, b);
        };
        pad.up = btn(SDL_GAMEPAD_BUTTON_DPAD_UP);
        pad.down = btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        pad.left = btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        pad.right = btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        pad.confirm = btn(SDL_GAMEPAD_BUTTON_SOUTH);
        pad.back = btn(SDL_GAMEPAD_BUTTON_EAST);
        pad.action = btn(SDL_GAMEPAD_BUTTON_NORTH);
        pad.alt = btn(SDL_GAMEPAD_BUTTON_WEST);
        pad.start = btn(SDL_GAMEPAD_BUTTON_START);
        pad.select = btn(SDL_GAMEPAD_BUTTON_BACK);
        pad.l1 = btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        pad.r1 = btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        pad.l3 = btn(SDL_GAMEPAD_BUTTON_LEFT_STICK);
        pad.r3 = btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        pad.leftX = normalizeAxis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX));
        pad.leftY = normalizeAxis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY));
        pad.rightX = normalizeAxis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX));
        pad.rightY = normalizeAxis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY));
        pad.trigL = normalizeTrigger(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        pad.trigR = normalizeTrigger(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    }
    return raw;
}

} // namespace comet::input
