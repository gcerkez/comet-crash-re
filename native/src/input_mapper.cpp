// input_mapper.cpp — see input_mapper.h. Merge order matters:
//   * Buttons are the OR of every source (any device can press Confirm).
//   * Movement prefers the gamepad's analog stick when it is deflected,
//     then the D-pad / movement keys as a digital vector.
//   * The mouse owns the cursor while it moves; otherwise the cursor is
//     integrated from the movement vector, like the original pad cursor.

#include "input_mapper.h"

#include <algorithm>
#include <cmath>

namespace comet::input {
namespace {

float clampUnit(float v) { return std::clamp(v, -1.0f, 1.0f); }

std::uint32_t buttonsFrom(const KeyboardState& kb) {
    std::uint32_t b = BtnNone;
    if (kb.confirm) b |= BtnConfirm;
    if (kb.back) b |= BtnBack;
    if (kb.action) b |= BtnAction;
    if (kb.alt) b |= BtnAlt;
    if (kb.start) b |= BtnStart;
    if (kb.select) b |= BtnSelect;
    if (kb.l1) b |= BtnL1;
    if (kb.r1) b |= BtnR1;
    if (kb.l2) b |= BtnL2;
    if (kb.r2) b |= BtnR2;
    if (kb.up) b |= BtnUp;
    if (kb.down) b |= BtnDown;
    if (kb.left) b |= BtnLeft;
    if (kb.right) b |= BtnRight;
    return b;
}

std::uint32_t buttonsFrom(const GamepadState& pad) {
    std::uint32_t b = BtnNone;
    if (pad.confirm) b |= BtnConfirm;
    if (pad.back) b |= BtnBack;
    if (pad.action) b |= BtnAction;
    if (pad.alt) b |= BtnAlt;
    if (pad.start) b |= BtnStart;
    if (pad.select) b |= BtnSelect;
    if (pad.l1) b |= BtnL1;
    if (pad.r1) b |= BtnR1;
    if (pad.l3) b |= BtnL3;
    if (pad.r3) b |= BtnR3;
    if (pad.up) b |= BtnUp;
    if (pad.down) b |= BtnDown;
    if (pad.left) b |= BtnLeft;
    if (pad.right) b |= BtnRight;
    return b;
}

} // namespace

void InputMapper::reset() {
    prevButtons_ = BtnNone;
    cursorInit_ = false;
}

InputFrame InputMapper::update(const RawInput& raw, float dtSeconds,
                               float viewportW, float viewportH) {
    InputFrame frame;

    // --- buttons: OR of keyboard, gamepad, and the mouse (LMB = Confirm,
    // RMB = Back, MMB = Action — the natural cursor-game mapping).
    std::uint32_t buttons = buttonsFrom(raw.keyboard) | buttonsFrom(raw.gamepad);
    if (raw.mouse.left) buttons |= BtnConfirm;
    if (raw.mouse.right) buttons |= BtnBack;
    if (raw.mouse.middle) buttons |= BtnAction;
    if (raw.gamepad.trigL > kTriggerThreshold) buttons |= BtnL2;
    if (raw.gamepad.trigR > kTriggerThreshold) buttons |= BtnR2;

    frame.buttons = buttons;
    frame.pressed = buttons & ~prevButtons_;
    frame.released = prevButtons_ & ~buttons;
    prevButtons_ = buttons;

    // --- movement: analog stick wins when deflected, else digital.
    float mx = 0.0f, my = 0.0f;
    const GamepadState& pad = raw.gamepad;
    if (std::fabs(pad.leftX) > 0.001f || std::fabs(pad.leftY) > 0.001f) {
        mx = pad.leftX;
        my = pad.leftY;
    } else {
        if (pad.left || raw.keyboard.left) mx -= 1.0f;
        if (pad.right || raw.keyboard.right) mx += 1.0f;
        if (pad.up || raw.keyboard.up) my -= 1.0f;
        if (pad.down || raw.keyboard.down) my += 1.0f;
        const float len = std::sqrt(mx * mx + my * my);
        if (len > 1.0f) {
            mx /= len;
            my /= len;
        }
    }
    frame.moveX = clampUnit(mx);
    frame.moveY = clampUnit(my);
    frame.aimX = clampUnit(pad.rightX);
    frame.aimY = clampUnit(pad.rightY);

    // --- cursor: mouse position when the mouse is in use, otherwise
    // integrate the movement vector (the original game's pad cursor).
    if (!cursorInit_) {
        cursorX_ = viewportW * 0.5f;
        cursorY_ = viewportH * 0.5f;
        cursorInit_ = true;
    }
    frame.mouseActive = raw.mouse.present && (raw.mouse.moved || raw.mouse.left ||
                                              raw.mouse.right || raw.mouse.wheel != 0);
    if (raw.mouse.present && raw.mouse.moved) {
        cursorX_ = raw.mouse.x;
        cursorY_ = raw.mouse.y;
    } else if (frame.moveX != 0.0f || frame.moveY != 0.0f) {
        cursorX_ += frame.moveX * kCursorSpeed * dtSeconds;
        cursorY_ += frame.moveY * kCursorSpeed * dtSeconds;
    }
    cursorX_ = std::clamp(cursorX_, 0.0f, std::max(0.0f, viewportW - 1.0f));
    cursorY_ = std::clamp(cursorY_, 0.0f, std::max(0.0f, viewportH - 1.0f));
    frame.hasCursor = true;
    frame.cursorX = cursorX_;
    frame.cursorY = cursorY_;
    frame.wheel = raw.mouse.wheel;

    frame.padConnected = raw.gamepad.connected;
    frame.padActive = raw.gamepad.connected &&
                      (buttonsFrom(raw.gamepad) != BtnNone ||
                       std::fabs(pad.leftX) > 0.001f || std::fabs(pad.leftY) > 0.001f ||
                       std::fabs(pad.rightX) > 0.001f || std::fabs(pad.rightY) > 0.001f);
    return frame;
}

} // namespace comet::input
