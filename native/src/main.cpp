// main.cpp — native host entry point (milestone 2: the game's menus).
//
// Frame loop:
//   SDL events -> RawInput -> InputMapper -> InputFrame -> MenuModel
//   MenuModel state -> SDL renderer (debug-text menus; the arena screen
//   keeps the milestone-1 placeholder grid as its body for now).
//
// The menus are the original's: same screens, same order, same strings
// (from the extracted language table). Gameplay rendering arrives with
// the PSGL shim in a later milestone; until then entering a mode lands
// on the arena placeholder with its briefing text and a working pause
// menu, which exercises the whole menu flow end to end.
//
// Env knobs (for smoke tests):
//   COMET_NATIVE_FRAMES=N  exit after N frames
//   COMET_NATIVE_LOG=1     one status line every 60 frames

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "input_mapper.h"
#include "level_data.h"
#include "menu_model.h"
#include "sdl_input.h"
#include "sfx_player.h"
#include "text_table.h"

using namespace comet::input;

namespace {

void drawText(SDL_Renderer* r, float x, float y, float scale,
              Uint8 cr, Uint8 cg, Uint8 cb, const std::string& str) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_SetRenderScale(r, scale, scale);
    SDL_RenderDebugText(r, x / scale, y / scale, str.c_str());
    SDL_SetRenderScale(r, 1.0f, 1.0f);
}

float textWidth(const std::string& s, float scale) {
    return (float)s.size() * 8.0f * scale;
}

void drawTextCentered(SDL_Renderer* r, float cx, float y, float scale,
                      Uint8 cr, Uint8 cg, Uint8 cb, const std::string& str) {
    drawText(r, cx - textWidth(str, scale) * 0.5f, y, scale, cr, cg, cb, str);
}

std::vector<std::string> wrapText(const std::string& text, int maxChars) {
    std::vector<std::string> lines;
    std::string flat = text;
    for (char& c : flat) {
        if (c == '\t') c = ' ';
    }
    size_t pos = 0;
    while (pos <= flat.size()) {
        size_t nl = flat.find('\n', pos);
        std::string para = flat.substr(pos, nl == std::string::npos ? nl : nl - pos);
        std::string line;
        size_t p = 0;
        while (true) {
            size_t sp = para.find(' ', p);
            std::string word = para.substr(p, sp == std::string::npos ? sp : sp - p);
            if (!line.empty() && (int)(line.size() + 1 + word.size()) > maxChars) {
                lines.push_back(line);
                line = word;
            } else {
                line = line.empty() ? word : line + " " + word;
            }
            if (sp == std::string::npos) break;
            p = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return lines;
}

void drawBody(SDL_Renderer* r, float x, float y, float w, float bottom,
              const std::string& body, int scroll) {
    const float scale = 2.0f;
    const float lineH = 21.0f;
    const int maxChars = (int)(w / (8.0f * scale));
    std::vector<std::string> lines = wrapText(body, maxChars > 8 ? maxChars : 8);
    float cy = y;
    for (int i = scroll; i < (int)lines.size(); i++) {
        if (cy + lineH > bottom) break;
        drawText(r, x, cy, scale, 205, 210, 220, lines[i]);
        cy += lineH;
    }
}

std::string itemLabel(const comet::MenuItem& it) {
    std::string label = it.labelStr >= 0 ? comet::text::text(it.labelStr) : it.label;
    if (it.suffixNumber >= 0) {
        label += " " + std::to_string(it.suffixNumber);
    }
    return label;
}

std::string itemValue(const comet::MenuItem& it) {
    using Kind = comet::MenuItem::Kind;
    switch (it.kind) {
        case Kind::Slider: {
            std::string bar = "[";
            const int span = it.maxValue - it.minValue;
            const int filled = span > 0 ? (it.value - it.minValue) * 10 / span : 0;
            for (int i = 0; i < 10; i++) bar += (i < filled ? '#' : '-');
            bar += "] " + std::to_string(it.value);
            return bar;
        }
        case Kind::Toggle:
            return it.value ? comet::text::text(comet::text::Enable)
                            : comet::text::text(comet::text::Disable);
        case Kind::Cycle: {
            std::string v;
            if (!it.cycleStrs.empty()) {
                v = comet::text::text(it.cycleStrs[it.value]);
            } else if (!it.cycleLiterals.empty()) {
                v = it.cycleLiterals[it.value];
            }
            return "< " + v + " >";
        }
        default:
            return "";
    }
}

void drawScreenTitle(SDL_Renderer* r, float W, const comet::Screen& s) {
    std::string title = s.titleStr >= 0 ? comet::text::text(s.titleStr) : s.titleLiteral;
    if (!title.empty()) {
        drawTextCentered(r, W * 0.5f, 54.0f, 3.0f, 240, 200, 90, title);
    }
}

void drawItemList(SDL_Renderer* r, float W, float H, const comet::Screen& s) {
    for (int i = 0; i < (int)s.items.size(); i++) {
        const comet::MenuItem& it = s.items[i];
        comet::FloatRect rc = comet::MenuModel::itemRect(W, H, i);
        const bool selected = (i == s.selected);
        if (it.kind == comet::MenuItem::Kind::Info) {
            drawText(r, rc.x + 10, rc.y + 8, 2.0f, 130, 140, 155, itemLabel(it));
            continue;
        }
        if (selected) {
            SDL_SetRenderDrawColor(r, 38, 62, 110, 255);
            SDL_FRect fr{rc.x, rc.y, rc.w, rc.h};
            SDL_RenderFillRect(r, &fr);
            SDL_SetRenderDrawColor(r, 240, 200, 90, 255);
            SDL_RenderRect(r, &fr);
        }
        drawText(r, rc.x + 14, rc.y + 8, 2.0f, 225, 230, 240, itemLabel(it));
        std::string value = itemValue(it);
        if (!value.empty()) {
            drawText(r, rc.x + rc.w - textWidth(value, 2.0f) - 14, rc.y + 8, 2.0f,
                     170, 205, 255, value);
        }
        if (selected && it.helpStr >= 0) {
            drawBody(r, W * 0.14f, H - 92.0f, W * 0.72f, H - 12.0f,
                     comet::text::text(it.helpStr), 0);
        }
    }
}

void drawArena(SDL_Renderer* r, float W, float H, const comet::MenuModel& model,
               const InputFrame& frame, bool dimmed,
               const comet::LevelData* level) {
    // Placeholder arena: grid + ship cursor, as in milestone 1.
    const float cell = H / 12.0f;
    SDL_SetRenderDrawColor(r, dimmed ? 18 : 30, dimmed ? 22 : 38, dimmed ? 30 : 52, 255);
    for (int gx = 0; gx <= 16; gx++) {
        SDL_RenderLine(r, gx * cell, 0, gx * cell, H);
    }
    for (int gy = 0; gy <= 12; gy++) {
        SDL_RenderLine(r, 0, gy * cell, 16 * cell, gy * cell);
    }
    // Real level data, when a .map was loaded (COMET_MAP): draw the
    // direction vectors the exploratory parser extracts (unit-magnitude
    // float pairs — the map's flow-field-like section) as ticks laid
    // out in file order, 40 per row.
    if (level != nullptr && !dimmed && !level->vectors.empty()) {
        SDL_SetRenderDrawColor(r, 235, 180, 90, 255);
        const float step = cell * 0.5f;
        int i = 0;
        for (const auto& v : level->vectors) {
            const float gx = (float)(i % 40) * step + step;
            const float gy = (float)(i / 40) * step + step;
            SDL_RenderLine(r, gx, gy, gx + v[0] * step * 0.45f,
                           gy + v[1] * step * 0.45f);
            ++i;
        }
    }
    const float cx = frame.cursorX, cy = frame.cursorY;
    const bool held = (frame.buttons & BtnConfirm) != 0;
    const bool tapped = (frame.pressed & BtnConfirm) != 0;
    if (tapped) {
        SDL_SetRenderDrawColor(r, 255, 190, 60, 255);
    } else if (held) {
        SDL_SetRenderDrawColor(r, 120, 230, 130, 255);
    } else {
        SDL_SetRenderDrawColor(r, 200, 210, 225, 255);
    }
    SDL_RenderLine(r, cx - 10, cy, cx + 10, cy);
    SDL_RenderLine(r, cx, cy - 10, cx, cy + 10);

    if (!model.arenaUiHidden() && !dimmed) {
        int title = model.arenaTitleStr();
        if (title >= 0) {
            drawText(r, 18, 14, 3.0f, 240, 200, 90, comet::text::text(title));
        }
        int body = model.arenaBodyStr();
        if (body >= 0) {
            drawBody(r, 18, 58, W * 0.62f, H * 0.6f, comet::text::text(body), 0);
        }
        drawText(r, 18, H - 30.0f, 2.0f, 130, 140, 155,
                 "START: PAUSE    START+BACK: QUIT");
        if (level != nullptr && !level->vectors.empty()) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "LEVEL VECTORS: %zu (EXPLORATORY PARSE)",
                          level->vectors.size());
            drawText(r, 18, H - 56.0f, 2.0f, 235, 180, 90, buf);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Comet Crash (native)", 960, 540, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return 1;
    }

    const char* framesEnv = SDL_getenv("COMET_NATIVE_FRAMES");
    const long maxFrames = framesEnv != nullptr ? std::atol(framesEnv) : 0;
    const bool logFrames = SDL_getenv("COMET_NATIVE_LOG") != nullptr;

    SdlInput sdlInput;
    InputMapper mapper;
    comet::MenuModel model;
    comet::LevelData level;
    bool haveLevel = false;
    if (const char* mapPath = SDL_getenv("COMET_MAP")) {
        haveLevel = level.loadFromFile(mapPath);
        if (logFrames) {
            SDL_Log("COMET_MAP %s: %s (%zu direction vectors)", mapPath,
                    haveLevel ? "loaded" : "FAILED", level.vectors.size());
        }
    }
    const comet::LevelData* levelArg = haveLevel ? &level : nullptr;
    comet::SfxPlayer sfx;
    if (const char* sfxDir = SDL_getenv("COMET_SFX_DIR")) {
        const int n = sfx.loadFromDir(sfxDir);
        if (logFrames) {
            SDL_Log("COMET_SFX_DIR %s: %d clip(s) loaded", sfxDir, n);
        }
        if (SDL_getenv("COMET_SFX_SELFTEST") != nullptr) {
            for (int i = 0; i < static_cast<int>(comet::Sfx::Count); ++i) {
                sfx.play(static_cast<comet::Sfx>(i));
            }
        }
    }
    bool running = true;
    long frameNo = 0;
    Uint64 lastTicks = SDL_GetTicks();

    while (running) {
        int winW = 960, winH = 540;
        SDL_GetWindowSize(window, &winW, &winH);

        sdlInput.beginFrame();
        if (sdlInput.quitRequested()) {
            running = false;
        }
        RawInput raw = sdlInput.sample();
        Uint64 nowTicks = SDL_GetTicks();
        float dt = (float)(nowTicks - lastTicks) / 1000.0f;
        lastTicks = nowTicks;
        if (dt > 0.1f) dt = 0.1f;
        InputFrame frame = mapper.update(raw, dt, (float)winW, (float)winH);

        // Host quit chord: Start+Back together (Esc remains the game's
        // Back button, per the milestone-1 input mapping).
        if ((frame.pressed & (BtnStart | BtnBack)) == (BtnStart | BtnBack)) {
            running = false;
        }

        model.update(frame, (float)winW, (float)winH);
        (void)model.takeAction();  // run-start hooks land with gameplay

        // One-shot SFX (MultiStream's job on PS3): the bank's own
        // item-choose click on menu confirms, its build sound on
        // arena placement taps.
        if ((frame.pressed & BtnConfirm) != 0) {
            if (model.current().id == comet::ScreenId::Arena) {
                sfx.play(comet::Sfx::Build);
            } else {
                sfx.play(comet::Sfx::Choose);
            }
        }

        SDL_SetRenderDrawColor(renderer, 8, 10, 16, 255);
        SDL_RenderClear(renderer);
        const float W = (float)winW, H = (float)winH;
        const comet::Screen& cur = model.current();

        if (model.arenaBehind() && cur.id != comet::ScreenId::Arena) {
            drawArena(renderer, W, H, model, frame, true, levelArg);
            comet::FloatRect panel = comet::MenuModel::panelRect(W, H);
            SDL_SetRenderDrawColor(renderer, 12, 16, 26, 235);
            SDL_FRect pf{panel.x, panel.y, panel.w, panel.h};
            SDL_RenderFillRect(renderer, &pf);
            SDL_SetRenderDrawColor(renderer, 90, 110, 150, 255);
            SDL_RenderRect(renderer, &pf);
            std::string title = cur.titleStr >= 0 ? comet::text::text(cur.titleStr)
                                                  : cur.titleLiteral;
            drawTextCentered(renderer, W * 0.5f, panel.y + 18, 3.0f, 240, 200, 90, title);
            drawItemList(renderer, W, H, cur);
        } else if (cur.id == comet::ScreenId::Arena) {
            drawArena(renderer, W, H, model, frame, false, levelArg);
        } else if (cur.id == comet::ScreenId::Title) {
            drawTextCentered(renderer, W * 0.5f, H * 0.30f, 6.0f, 240, 200, 90,
                             cur.titleLiteral);
            if ((frameNo / 32) % 2 == 0) {
                drawTextCentered(renderer, W * 0.5f, H * 0.52f, 2.0f, 225, 230, 240,
                                 "PRESS START");
            }
            drawTextCentered(renderer, W * 0.5f, H - 40.0f, 2.0f, 130, 140, 155,
                             comet::text::text(comet::text::CreditsBy));
        } else {
            drawScreenTitle(renderer, W, cur);
            if (!cur.items.empty()) {
                drawItemList(renderer, W, H, cur);
            }
            std::string body = cur.bodyStr >= 0 ? comet::text::text(cur.bodyStr)
                                                : cur.bodyLiteral;
            if (!body.empty()) {
                const float top = cur.items.empty() ? H * 0.24f : H * 0.62f;
                drawBody(renderer, W * 0.16f, top, W * 0.68f, H - 46.0f, body,
                         cur.scroll);
            }
            drawText(renderer, 16, H - 30.0f, 2.0f, 110, 120, 138,
                     comet::text::text(comet::text::MoveSelect));
        }

        SDL_RenderPresent(renderer);
        frameNo++;
        if (logFrames && frameNo % 60 == 0) {
            SDL_Log("frame %ld screen=%d selected=%d buttons=%08x cursor=%.0f,%.0f",
                    frameNo, (int)cur.id, cur.selected, frame.buttons, frame.cursorX,
                    frame.cursorY);
        }
        if (maxFrames > 0 && frameNo >= maxFrames) {
            SDL_Log("exited cleanly after %ld frames", frameNo);
            running = false;
        }
        SDL_Delay(16);
    }

    sfx.shutdown(); // before SDL_Quit: the stream must not outlive it
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
