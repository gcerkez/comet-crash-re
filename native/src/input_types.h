// input_types.h — the unified input model for the native host.
//
// The PS3 original reads controllers through the low-level `sys_io`
// library (the ioPad* family: digital buttons + two analog sticks, sampled
// once per frame). The game logic therefore thinks in terms of "pad
// frames". This header defines the native equivalent: every physical
// source (keyboard, mouse, modern gamepad) is translated into one
// InputFrame per player, so reconstructed game code never needs to know
// which device produced the input.
//
// Button bits deliberately mirror the PS3 pad layout (Cross = Confirm,
// Circle = Back, ...) so mappings stay recognizable when we line the
// native code up against the decompiled original.

#pragma once

#include <cstdint>

namespace comet::input {

// Logical buttons, one bit each. Names follow the PS3 pad; the comment
// gives the role Comet Crash assigns them in menus/gameplay.
enum Button : std::uint32_t {
    BtnNone    = 0,
    BtnConfirm = 1u << 0,  // Cross (X)    — select / build / place
    BtnBack    = 1u << 1,  // Circle (O)   — back / cancel / sell
    BtnAction  = 1u << 2,  // Triangle     — context action
    BtnAlt     = 1u << 3,  // Square       — context action
    BtnL1      = 1u << 4,
    BtnR1      = 1u << 5,
    BtnL2      = 1u << 6,
    BtnR2      = 1u << 7,
    BtnStart   = 1u << 8,  // pause / menu
    BtnSelect  = 1u << 9,
    BtnUp      = 1u << 10, // D-pad
    BtnDown    = 1u << 11,
    BtnLeft    = 1u << 12,
    BtnRight   = 1u << 13,
    BtnL3      = 1u << 14,
    BtnR3      = 1u << 15,
};

// One frame of merged input for one player.
struct InputFrame {
    std::uint32_t buttons = BtnNone; // currently held
    std::uint32_t pressed = BtnNone; // went down this frame (edge)
    std::uint32_t released = BtnNone; // went up this frame (edge)

    // Merged movement vector in -1..1: D-pad, left stick, or movement
    // keys, whichever is active. This is what menu/build cursors use when
    // no mouse is driving them.
    float moveX = 0.0f;
    float moveY = 0.0f;

    // Right stick, kept separate for camera-style uses.
    float aimX = 0.0f;
    float aimY = 0.0f;

    // Absolute cursor (window pixels). Meaningful only when hasCursor is
    // true. The mouse owns it while it moves; otherwise the mapper
    // integrates moveX/moveY into it so pad/keyboard users get a cursor
    // too — the original game is cursor-driven with a pad, and the native
    // build keeps that model.
    bool hasCursor = false;
    float cursorX = 0.0f;
    float cursorY = 0.0f;

    // Mouse wheel steps accumulated this frame (+ up, - down).
    int wheel = 0;

    // Which source produced this frame (debug/telemetry).
    bool padConnected = false;
    bool padActive = false;   // a gamepad contributed this frame
    bool mouseActive = false; // the mouse contributed this frame
};

// ---- Raw source states -------------------------------------------------
// The mapper consumes these plain structs; only the SDL backend knows
// about SDL types, which keeps the mapping logic unit-testable.

struct KeyboardState {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, back = false, action = false, alt = false;
    bool start = false, select = false;
    bool l1 = false, r1 = false, l2 = false, r2 = false;
};

struct MouseState {
    bool present = false; // a mouse moved/clicked at least once
    bool moved = false;   // position changed since the previous frame
    float x = 0.0f, y = 0.0f;
    bool left = false, right = false, middle = false;
    int wheel = 0; // accumulated wheel steps since the previous frame
};

struct GamepadState {
    bool connected = false;
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, back = false, action = false, alt = false;
    bool start = false, select = false;
    bool l1 = false, r1 = false, l3 = false, r3 = false;
    float leftX = 0.0f, leftY = 0.0f;   // -1..1, deadzone applied by backend
    float rightX = 0.0f, rightY = 0.0f; // -1..1
    float trigL = 0.0f, trigR = 0.0f;   // 0..1
};

struct RawInput {
    KeyboardState keyboard;
    MouseState mouse;
    GamepadState gamepad;
};

} // namespace comet::input
