// input_mapper_test.cpp — headless checks for the input merge logic.
// Feeds synthetic raw states through InputMapper and verifies the unified
// frames: keyboard mapping, button edges, mouse cursor ownership, gamepad
// stick/dpad/trigger handling, and the integrated pad cursor.

#include <cmath>
#include <cstdio>

#include "../src/input_mapper.h"

using namespace comet::input;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    const float W = 960.0f, H = 540.0f;

    { // Keyboard: arrows move, Enter confirms with a one-frame edge.
        InputMapper m;
        RawInput raw;
        raw.keyboard.right = true;
        InputFrame f = m.update(raw, 0.016f, W, H);
        CHECK(near(f.moveX, 1.0f) && near(f.moveY, 0.0f));
        CHECK((f.buttons & BtnRight) != 0);

        raw.keyboard.confirm = true;
        f = m.update(raw, 0.016f, W, H);
        CHECK((f.buttons & BtnConfirm) != 0);
        CHECK((f.pressed & BtnConfirm) != 0); // edge on the first frame
        f = m.update(raw, 0.016f, W, H);
        CHECK((f.buttons & BtnConfirm) != 0);
        CHECK((f.pressed & BtnConfirm) == 0); // held: no new edge
        raw.keyboard.confirm = false;
        f = m.update(raw, 0.016f, W, H);
        CHECK((f.released & BtnConfirm) != 0);
    }

    { // Mouse: owns the cursor while it moves; LMB = Confirm; wheel passes.
        InputMapper m;
        RawInput raw;
        raw.mouse.present = true;
        raw.mouse.moved = true;
        raw.mouse.x = 100.0f;
        raw.mouse.y = 50.0f;
        raw.mouse.left = true;
        raw.mouse.wheel = 2;
        InputFrame f = m.update(raw, 0.016f, W, H);
        CHECK(near(f.cursorX, 100.0f) && near(f.cursorY, 50.0f));
        CHECK((f.buttons & BtnConfirm) != 0);
        CHECK(f.wheel == 2);
        CHECK(f.mouseActive);
    }

    { // Keys/stick integrate the cursor when the mouse is idle:
      // start at the center (480) and hold right for 0.5 s at 620 px/s.
        InputMapper m;
        RawInput raw;
        raw.keyboard.right = true;
        InputFrame f = m.update(raw, 0.5f, W, H);
        CHECK(near(f.cursorX, 480.0f + 310.0f));
        CHECK(near(f.cursorY, 270.0f));
    }

    { // Gamepad: stick moves, South confirms, trigger past threshold = R2,
      // D-pad still works, pad reports active.
        InputMapper m;
        RawInput raw;
        raw.gamepad.connected = true;
        raw.gamepad.leftX = 0.5f;
        raw.gamepad.confirm = true;
        raw.gamepad.trigR = 0.9f;
        InputFrame f = m.update(raw, 0.016f, W, H);
        CHECK(near(f.moveX, 0.5f));
        CHECK((f.buttons & BtnConfirm) != 0);
        CHECK((f.buttons & BtnR2) != 0);
        CHECK(f.padConnected && f.padActive);

        raw.gamepad.leftX = 0.0f;
        raw.gamepad.confirm = false;
        raw.gamepad.trigR = 0.0f;
        raw.gamepad.up = true;
        f = m.update(raw, 0.016f, W, H);
        CHECK(near(f.moveY, -1.0f));
        CHECK((f.buttons & BtnUp) != 0);
    }

    { // Diagonal digital movement is normalized.
        InputMapper m;
        RawInput raw;
        raw.keyboard.right = true;
        raw.keyboard.down = true;
        InputFrame f = m.update(raw, 0.016f, W, H);
        CHECK(near(std::sqrt(f.moveX * f.moveX + f.moveY * f.moveY), 1.0f));
    }

    if (failures == 0) {
        std::printf("input_mapper_test: all checks passed\n");
        return 0;
    }
    std::printf("input_mapper_test: %d check(s) failed\n", failures);
    return 1;
}
