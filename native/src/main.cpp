// main.cpp — Comet Crash native host, milestone 1: a window and the
// unified input layer. This is the shell the reconstruction will grow
// into: boot -> frame loop -> (eventually) the game's own update/render,
// with every PS3 service replaced by a host equivalent behind the same
// shape of interface (see notes/NATIVE-PORT-001.md for the full map).
//
// For now the "game" is a debug view: a placeholder arena grid, a cursor
// driven by mouse / keys / stick, and lamps for every logical button, so
// input behavior can be verified by eye on any of the three sources.
//
// Env knobs for smoke tests: COMET_NATIVE_FRAMES=N exits after N frames;
// COMET_NATIVE_LOG=1 prints a one-line input summary every 60 frames.

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "input_mapper.h"
#include "sdl_input.h"

using namespace comet::input;

namespace {

void drawButtonLamp(SDL_Renderer* r, float x, float y, const char* label,
                    bool held, bool pressed) {
    if (pressed) {
        SDL_SetRenderDrawColor(r, 255, 210, 80, 255);
    } else if (held) {
        SDL_SetRenderDrawColor(r, 90, 200, 120, 255);
    } else {
        SDL_SetRenderDrawColor(r, 70, 76, 92, 255);
    }
    SDL_FRect box{x, y, 26.0f, 26.0f};
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 220, 226, 240, 255);
    SDL_RenderDebugText(r, x + 2.0f, y + 34.0f, label);
}

std::string envOrEmpty(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "Comet Crash — native reconstruction host", 960, 540, 0);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const long maxFrames = std::atol(envOrEmpty("COMET_NATIVE_FRAMES").c_str());
    const bool logFrames = envOrEmpty("COMET_NATIVE_LOG") == "1";

    SdlInput sdlInput;
    InputMapper mapper;
    long frameNo = 0;
    Uint64 lastTicks = SDL_GetTicksNS();
    bool running = true;

    while (running) {
        const Uint64 now = SDL_GetTicksNS();
        float dt = static_cast<float>(now - lastTicks) / 1.0e9f;
        lastTicks = now;
        if (dt > 0.1f) dt = 0.1f; // clamp hitches; the game loop will fixed-step later
        if (dt <= 0.0f) dt = 0.0001f;

        sdlInput.beginFrame();
        if (sdlInput.quitRequested()) break;

        int winW = 960, winH = 540;
        SDL_GetWindowSize(window, &winW, &winH);
        const RawInput raw = sdlInput.sample();
        const InputFrame frame =
            mapper.update(raw, dt, static_cast<float>(winW), static_cast<float>(winH));

        // Start+Back together is the host-level quit chord (the PS button
        // equivalent); Esc alone stays the game's Back button.
        if ((frame.buttons & (BtnStart | BtnBack)) == (BtnStart | BtnBack)) break;

        // ---- render the debug view
        SDL_SetRenderDrawColor(renderer, 16, 18, 26, 255);
        SDL_RenderClear(renderer);

        // Placeholder arena grid (the real arena renderer replaces this).
        SDL_SetRenderDrawColor(renderer, 38, 44, 62, 255);
        for (int gx = 0; gx <= winW; gx += 60) {
            SDL_RenderLine(renderer, static_cast<float>(gx), 0.0f,
                           static_cast<float>(gx), static_cast<float>(winH));
        }
        for (int gy = 0; gy <= winH; gy += 60) {
            SDL_RenderLine(renderer, 0.0f, static_cast<float>(gy),
                           static_cast<float>(winW), static_cast<float>(gy));
        }

        // Cursor crosshair: green while Confirm is held, amber on the
        // frame it was pressed, white otherwise.
        if (frame.pressed & BtnConfirm) {
            SDL_SetRenderDrawColor(renderer, 255, 210, 80, 255);
        } else if (frame.buttons & BtnConfirm) {
            SDL_SetRenderDrawColor(renderer, 90, 220, 130, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 235, 240, 250, 255);
        }
        const float cx = frame.cursorX, cy = frame.cursorY;
        SDL_RenderLine(renderer, cx - 12.0f, cy, cx + 12.0f, cy);
        SDL_RenderLine(renderer, cx, cy - 12.0f, cx, cy + 12.0f);

        // Button lamps + labels along the bottom.
        struct LampDef {
            const char* label;
            Button bit;
        };
        const LampDef lamps[] = {
            {"X", BtnConfirm}, {"O", BtnBack},   {"TRI", BtnAction},
            {"SQR", BtnAlt},   {"L1", BtnL1},    {"R1", BtnR1},
            {"L2", BtnL2},     {"R2", BtnR2},    {"STA", BtnStart},
            {"SEL", BtnSelect},{"UP", BtnUp},    {"DN", BtnDown},
            {"LF", BtnLeft},   {"RT", BtnRight},
        };
        float lx = 16.0f;
        const float ly = static_cast<float>(winH) - 78.0f;
        for (const LampDef& lamp : lamps) {
            drawButtonLamp(renderer, lx, ly, lamp.label,
                           (frame.buttons & lamp.bit) != 0,
                           (frame.pressed & lamp.bit) != 0);
            lx += 42.0f;
        }

        // Status + help text.
        SDL_SetRenderDrawColor(renderer, 200, 208, 224, 255);
        char status[256];
        std::snprintf(status, sizeof(status),
                      "cursor %5.0f,%5.0f  move %+0.2f,%+0.2f  wheel %+d  "
                      "pad: %s%s  mouse: %s",
                      frame.cursorX, frame.cursorY, frame.moveX, frame.moveY,
                      frame.wheel, frame.padConnected ? "connected" : "none",
                      frame.padActive ? " (active)" : "",
                      frame.mouseActive ? "active" : "idle");
        SDL_RenderDebugText(renderer, 16.0f, 14.0f, status);
        SDL_RenderDebugText(renderer, 16.0f, 30.0f,
                            "Mouse drives the cursor; arrows/WASD move it too. "
                            "Enter=Confirm Esc=Back P=Start. Start+Back quits.");

        SDL_RenderPresent(renderer);

        if (logFrames && frameNo % 60 == 0) {
            std::printf("frame %ld buttons=%08x pressed=%08x cursor=%.0f,%.0f "
                        "move=%.2f,%.2f pad=%d\n",
                        frameNo, frame.buttons, frame.pressed, frame.cursorX,
                        frame.cursorY, frame.moveX, frame.moveY,
                        frame.padConnected ? 1 : 0);
            std::fflush(stdout);
        }

        ++frameNo;
        if (maxFrames > 0 && frameNo >= maxFrames) break;
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("comet_native: exited cleanly after %ld frames\n", frameNo);
    return 0;
}
