// input_mapper.h — merges RawInput (keyboard + mouse + gamepad) into the
// unified InputFrame the game consumes. Pure logic, no SDL: the SDL
// backend fills RawInput, this class does the merging, edge detection and
// cursor integration. Unit-tested by tests/input_mapper_test.cpp.

#pragma once

#include "input_types.h"

namespace comet::input {

class InputMapper {
public:
    // viewportW/H bound the cursor. Call once per frame with the elapsed
    // time so the stick/keys-driven cursor moves at a steady speed.
    InputFrame update(const RawInput& raw, float dtSeconds,
                      float viewportW, float viewportH);

    void reset();

private:
    std::uint32_t prevButtons_ = BtnNone;
    float cursorX_ = 0.0f;
    float cursorY_ = 0.0f;
    bool cursorInit_ = false;

    // Pixels per second for the keys/stick-driven cursor.
    static constexpr float kCursorSpeed = 620.0f;
    static constexpr float kTriggerThreshold = 0.35f;
};

} // namespace comet::input
