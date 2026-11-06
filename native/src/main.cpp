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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "input_mapper.h"
#include "level_data.h"
#include "obj_model.h"
#include "arena_sim.h"
#include "dds_texture.h"

#include <cstring>
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


// ---- projected-3D scene (the PSGL stand-in for now) ----
// PSGL is OpenGL ES 1.x; this host renderer plays the same role with
// CPU projection + SDL_RenderGeometry triangles. Presentation now
// matches the original game (Jerry, 2026-11-06): a FIXED, steeply
// tilted top-down camera — the map never moves — and the game's
// own assets: DDS-textured OBJ models for the ship and units, and
// the structure/agent DDS artwork as standing billboards (the
// structure meshes themselves are not among the shipped files;
// only fighter02.obj and scout02.obj are).

struct Vec3f { float x, y, z; };

Vec3f vsub(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3f vcross(Vec3f a, Vec3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float vdot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3f vnorm(Vec3f a) {
    const float l = std::sqrt(vdot(a, a));
    return l > 0 ? Vec3f{a.x / l, a.y / l, a.z / l} : a;
}

struct Camera {
    Vec3f eye, fwd, right, up;
    float focal; // in pixels
};

// Fixed camera: high, south of the field, looking slightly past
// centre. Nothing here depends on time — the map is static.
Camera makeCamera(float W, float H, float halfExtent) {
    const float S = halfExtent * 2.0f;
    Camera c;
    c.eye = {0.0f, S * 0.80f, halfExtent + S * 0.55f};
    const Vec3f target{0.0f, 0.0f, -S * 0.02f};
    c.fwd = vnorm(vsub(target, c.eye));
    c.right = vnorm(vcross(c.fwd, {0, 1, 0}));
    c.up = vcross(c.right, c.fwd);
    c.focal = (H * 0.5f) / std::tan(0.45f); // ~52 deg vertical fov
    (void)W;
    return c;
}

// Returns false when behind the camera.
bool project(const Camera& c, float W, float H, Vec3f p, float& sx, float& sy,
             float& depth) {
    const Vec3f d = vsub(p, c.eye);
    depth = vdot(d, c.fwd);
    if (depth < 0.5f) {
        return false;
    }
    sx = W * 0.5f + vdot(d, c.right) * c.focal / depth;
    sy = H * 0.5f - vdot(d, c.up) * c.focal / depth;
    return true;
}

// The game's artwork, resolved from the model directory at start.
struct SceneAssets {
    comet::ObjModel scout;
    bool haveScout = false;
    SDL_Texture* shipTex = nullptr;
    SDL_Texture* scoutTex = nullptr;
    SDL_Texture* groundTex = nullptr;
    SDL_Texture* rockTex = nullptr;
    SDL_Texture* structTex[29] = {}; // by structure kind
};

std::string dirOf(const std::string& p) {
    const std::size_t i = p.find_last_of('/');
    return i == std::string::npos ? std::string(".") : p.substr(0, i);
}

SDL_Texture* loadDdsTexture(SDL_Renderer* r, const std::string& path) {
    comet::RgbaImage img;
    if (!comet::loadDdsFile(path.c_str(), img)) {
        return nullptr;
    }
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STATIC, img.width,
                                       img.height);
    if (t == nullptr) {
        return nullptr;
    }
    SDL_UpdateTexture(t, nullptr, img.rgba.data(), img.width * 4);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

// The diffuse texture a model's sibling .mtl names in map_Kd.
std::string mtlTexturePath(const std::string& objPath) {
    const std::size_t dot = objPath.find_last_of('.');
    const std::string mtlPath = objPath.substr(0, dot) + ".mtl";
    std::FILE* f = std::fopen(mtlPath.c_str(), "rb");
    if (f == nullptr) {
        return std::string();
    }
    std::string found;
    char line[512];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        const char* p = std::strstr(line, "map_Kd");
        if (p != nullptr) {
            p += 6;
            while (*p == ' ' || *p == '\t') {
                ++p;
            }
            std::string name(p);
            while (!name.empty() &&
                   (name.back() == '\n' || name.back() == '\r' ||
                    name.back() == ' ')) {
                name.pop_back();
            }
            if (!name.empty()) {
                found = dirOf(objPath) + "/" + name;
                break;
            }
        }
    }
    std::fclose(f);
    return found;
}

void buildSceneAssets(SDL_Renderer* r, const std::string& modelPath,
                      SceneAssets& A) {
    const std::string dir = dirOf(modelPath);        // .../data/models/care
    const std::string dataRoot = dirOf(dirOf(dir));  // .../data
    const std::string shipTexPath = mtlTexturePath(modelPath);
    if (!shipTexPath.empty()) {
        A.shipTex = loadDdsTexture(r, shipTexPath);
    }
    const std::string scoutPath = dir + "/scout02.obj";
    if (A.scout.loadFromFile(scoutPath.c_str())) {
        A.haveScout = true;
        const std::string st = mtlTexturePath(scoutPath);
        if (!st.empty()) {
            A.scoutTex = loadDdsTexture(r, st);
        }
    }
    A.groundTex = loadDdsTexture(
        r, dataRoot + "/models/care/cometGaseous/cometGaseous_DC.dds");
    A.rockTex = A.groundTex;
    auto S = [&r, &A, &dataRoot](int kind, const char* rel) {
        A.structTex[kind] = loadDdsTexture(r, dataRoot + rel);
    };
    // Cosmetic kind -> artwork mapping (the names are the game's).
    S(20, "/models/care/weapons/structBasePlat_DC.dds"); // Base
    S(21, "/models/care/weapons/structMissile_DC.dds");  // Turret
    S(22, "/models/care/stunner_DC.dds");                // Pulsar
    S(23, "/models/care/weapons/structLaser_DC.dds");    // Laser
    S(24, "/models/care/weapons/structMissile_DC.dds");  // Bomber
    S(25, "/models/care/converter_DC.dds");              // Gateway
    S(26, "/models/care/structures/structBeacon_DC.dds"); // Basic Ops
    S(28, "/models/care/structures/structBeacon_DC.dds"); // Special Ops
    SDL_Log("scene assets: shipTex=%d scout=%d groundTex=%d",
            A.shipTex != nullptr, (int)A.haveScout, A.groundTex != nullptr);
}

// A painter-sorted sink of projected, textured triangles.
struct TriSink {
    struct Item {
        SDL_Texture* tex;
        SDL_Vertex v[3];
        float depth;
    };
    std::vector<Item> items;

    void addTri(SDL_Texture* tex, const Camera& cam, float W, float H, Vec3f a,
                Vec3f b, Vec3f c, SDL_FPoint ua, SDL_FPoint ub, SDL_FPoint uc,
                SDL_FColor col) {
        Item it{};
        it.tex = tex;
        const Vec3f pts[3] = {a, b, c};
        const SDL_FPoint uvs[3] = {ua, ub, uc};
        float dep = 0;
        for (int k = 0; k < 3; ++k) {
            float d;
            if (!project(cam, W, H, pts[k], it.v[k].position.x,
                         it.v[k].position.y, d)) {
                return;
            }
            dep += d;
            it.v[k].color = col;
            it.v[k].tex_coord = uvs[k];
        }
        it.depth = dep / 3.0f;
        items.push_back(it);
    }

    void addQuad(SDL_Texture* tex, const Camera& cam, float W, float H,
                 Vec3f a, Vec3f b, Vec3f c, Vec3f d, SDL_FColor col) {
        addTri(tex, cam, W, H, a, b, c, {0, 0}, {1, 0}, {1, 1}, col);
        addTri(tex, cam, W, H, a, c, d, {0, 0}, {1, 1}, {0, 1}, col);
    }

    // A textured OBJ model placed in the world (lambert-shaded,
    // vertex color = tint x shade).
    void addModel(SDL_Texture* tex, const Camera& cam, float W, float H,
                  const comet::ObjModel& obj, float wx, float wy, float wz,
                  float yaw, float scale, SDL_FColor tint) {
        const float cs = std::cos(yaw), sn = std::sin(yaw);
        const Vec3f light = vnorm({0.5f, 0.8f, 0.3f});
        for (std::size_t ti = 0; ti < obj.triangles.size(); ++ti) {
            const auto& tr = obj.triangles[ti];
            Vec3f wp[3];
            SDL_FPoint uv[3];
            for (int k = 0; k < 3; ++k) {
                const auto& p = obj.positions[tr.v[k]];
                wp[k] = {p[0] * scale * cs - p[2] * scale * sn + wx,
                         p[1] * scale + wy,
                         p[0] * scale * sn + p[2] * scale * cs + wz};
                if (tex != nullptr && tr.t[k] >= 0 &&
                    tr.t[k] < (int)obj.uvs.size()) {
                    const auto& t2 = obj.uvs[tr.t[k]];
                    uv[k] = {t2[0], 1.0f - t2[1]}; // DDS rows are top-first
                } else {
                    uv[k] = {0, 0};
                }
            }
            const auto& fn = obj.faceNormals[ti];
            const Vec3f n{fn[0] * cs - fn[2] * sn, fn[1],
                          fn[0] * sn + fn[2] * cs};
            float lam = vdot(n, light);
            if (lam < 0) {
                lam = -lam * 0.6f;
            }
            const float shade = 0.35f + 0.65f * lam;
            const SDL_FColor col{tint.r * shade, tint.g * shade,
                                 tint.b * shade, tint.a};
            addTri(tex, cam, W, H, wp[0], wp[1], wp[2], uv[0], uv[1], uv[2],
                   col);
        }
    }

    // A camera-facing (around Y) standing quad — how the structure
    // artwork is presented until their meshes are recovered.
    void addBillboard(SDL_Texture* tex, const Camera& cam, float W, float H,
                      float cx, float cz, float baseY, float w, float hgt,
                      SDL_FColor col) {
        Vec3f right{cam.right.x, 0.0f, cam.right.z};
        right = vnorm(right);
        const Vec3f bl{cx - right.x * w * 0.5f, baseY, cz - right.z * w * 0.5f};
        const Vec3f br{cx + right.x * w * 0.5f, baseY, cz + right.z * w * 0.5f};
        const Vec3f tl{bl.x, baseY + hgt, bl.z};
        const Vec3f tr{br.x, baseY + hgt, br.z};
        addQuad(tex, cam, W, H, bl, br, tr, tl, col);
    }

    void flush(SDL_Renderer* r) {
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return a.depth > b.depth;
        });
        for (const Item& it : items) {
            SDL_RenderGeometry(r, it.tex, it.v, 3, nullptr, 0);
        }
        items.clear();
    }
};

float cellHash(int x, int z) {
    const unsigned h = (unsigned)(x * 73856093) ^ (unsigned)(z * 19349663);
    return (float)(h % 100) / 100.0f;
}

void drawScene3D(SDL_Renderer* r, float W, float H, const comet::ObjModel& obj,
                 const comet::LevelData* level, double t,
                 const comet::ArenaSim* sim, const SceneAssets& A) {
    float extent = 12.0f; // half-extent in world units (1 cell = 1 unit)
    if (level != nullptr) {
        extent = (float)level->extent * 0.5f;
    }
    const Camera cam = makeCamera(W, H, extent);
    TriSink sink;

    // Ground: one textured quad per cell (the comet surface art,
    // repeated) with per-cell tint variation, over a dark apron.
    const int cells = (int)(extent * 2.0f);
    sink.addQuad(nullptr, cam, W, H, {-extent - 2, -0.05f, -extent - 2},
                 {extent + 2, -0.05f, -extent - 2},
                 {extent + 2, -0.05f, extent + 2},
                 {-extent - 2, -0.05f, extent + 2},
                 {0.03f, 0.04f, 0.06f, 1.0f});
    for (int gz = 0; gz < cells; ++gz) {
        for (int gx = 0; gx < cells; ++gx) {
            const float x0 = gx - extent, z0 = gz - extent;
            const float v = 0.55f + 0.40f * cellHash(gx, gz);
            const SDL_FColor col = A.groundTex != nullptr
                                       ? SDL_FColor{v, v, v * 0.96f, 1.0f}
                                       : SDL_FColor{0.05f * v + 0.05f,
                                                    0.08f * v + 0.06f,
                                                    0.13f * v + 0.08f, 1.0f};
            sink.addQuad(A.groundTex, cam, W, H, {x0, 0, z0}, {x0 + 1, 0, z0},
                         {x0 + 1, 0, z0 + 1}, {x0, 0, z0 + 1}, col);
        }
    }

    if (sim != nullptr) {
        const float half = sim->extentCells * 0.5f;
        // Base pads.
        auto basePad = [&](float bx, float bz, bool player) {
            const float cx = bx - half, cz = bz - half;
            const SDL_FColor col = player ? SDL_FColor{0.10f, 0.45f, 0.42f, 1}
                                          : SDL_FColor{0.50f, 0.14f, 0.10f, 1};
            sink.addQuad(nullptr, cam, W, H, {cx - 0.8f, 0.02f, cz - 0.8f},
                         {cx + 0.8f, 0.02f, cz - 0.8f},
                         {cx + 0.8f, 0.02f, cz + 0.8f},
                         {cx - 0.8f, 0.02f, cz + 0.8f}, col);
            sink.addBillboard(A.structTex[20], cam, W, H, cx, cz, 0.0f, 1.6f,
                              1.25f, {1, 1, 1, 1});
        };
        basePad(sim->playerBaseX, sim->playerBaseZ, true);
        basePad(sim->enemyBaseX, sim->enemyBaseZ, false);

        // Placed content: terrain rocks and structures as billboards
        // with the game's artwork.
        for (const auto& s : sim->structures) {
            const float cx = s.x + 0.5f - half, cz = s.y + 0.5f - half;
            if (s.rock) {
                sink.addBillboard(A.rockTex, cam, W, H, cx, cz, 0.0f, 1.3f,
                                  1.05f, {0.62f, 0.57f, 0.50f, 1.0f});
                continue;
            }
            if (s.kind < 20 || s.kind > 28) {
                continue;
            }
            SDL_Texture* tex = A.structTex[s.kind];
            float wdt = 1.05f, hgt = 1.0f;
            if (s.kind == 20) {
                wdt = 1.5f;
                hgt = 1.2f;
            } else if (s.kind == 26 || s.kind == 28) {
                wdt = 1.25f;
                hgt = 1.1f;
            }
            SDL_FColor col;
            if (s.state == 0) { // dormant site: low, dark silhouette
                hgt *= 0.5f;
                col = {0.38f, 0.38f, 0.44f, 1.0f};
            } else if (s.state == 1) { // building: pulse
                const float p = 0.8f + 0.2f * std::sin((float)t * 6.0f);
                col = {p, p, p, 1.0f};
            } else { // active: side tint
                col = s.owner == 1 ? SDL_FColor{0.78f, 1.0f, 0.96f, 1.0f}
                                    : SDL_FColor{1.0f, 0.80f, 0.68f, 1.0f};
            }
            sink.addBillboard(tex, cam, W, H, cx, cz, 0.0f, wdt, hgt, col);
        }

        // Units: the scout model (textured); flying rocks billboarded.
        for (const auto& e : sim->entities) {
            const float wx = e.x - half, wz = e.y - half;
            if (e.kind == 90) {
                sink.addBillboard(A.rockTex, cam, W, H, wx, wz, 0.15f, 0.85f,
                                  0.8f, {1.0f, 1.0f, 1.0f, 1.0f});
                continue;
            }
            const SDL_FColor tint = e.playerOwned
                                        ? SDL_FColor{0.72f, 1.0f, 0.96f, 1.0f}
                                        : SDL_FColor{1.0f, 0.80f, 0.68f, 1.0f};
            if (A.haveScout) {
                float yaw = e.playerOwned ? -1.5708f : 1.5708f;
                if (e.pathIdx < (int)e.path.size()) {
                    const float dx = e.path[e.pathIdx].first - e.x;
                    const float dz = e.path[e.pathIdx].second - e.y;
                    if (dx * dx + dz * dz > 0.0001f) {
                        yaw = std::atan2(dz, dx);
                    }
                }
                const float sc = A.scout.radius > 0 ? 0.5f / A.scout.radius : 1;
                sink.addModel(A.scoutTex, cam, W, H, A.scout, wx, 0.30f, wz,
                              yaw, sc, tint);
            } else {
                sink.addBillboard(A.scoutTex, cam, W, H, wx, wz, 0.0f, 0.8f,
                                  0.7f, tint);
            }
        }

        // The player's ship: the fighter model with its own texture.
        if (sim->shipAlive && !obj.empty()) {
            const float sc = obj.radius > 0 ? 0.85f / obj.radius : 1.0f;
            sink.addModel(A.shipTex, cam, W, H, obj, sim->shipX - half, 0.55f,
                          sim->shipY - half, sim->shipYaw, sc,
                          {1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    sink.flush(r);

    // Shots on top.
    if (sim != nullptr) {
        const float half = sim->extentCells * 0.5f;
        SDL_SetRenderDrawColor(r, 255, 230, 150, 255);
        for (const auto& s2 : sim->shots) {
            float sx, sy, dep;
            if (project(cam, W, H, {s2.x - half, 0.8f, s2.y - half}, sx, sy,
                        dep)) {
                SDL_RenderPoint(r, sx, sy);
            }
        }
    }
}


void drawArena(SDL_Renderer* r, float W, float H, const comet::MenuModel& model,
               const InputFrame& frame, bool dimmed,
               const comet::LevelData* level, const comet::ObjModel* objModel,
               double timeSec, const comet::ArenaSim* sim,
               const SceneAssets& assets) {
    // With a model loaded, the arena is the projected 3D scene;
    // otherwise the milestone-1 grid + parsed-record overlay.
    const bool scene = (objModel != nullptr && !objModel->empty() && !dimmed);
    if (scene) {
        drawScene3D(r, W, H, *objModel, level, timeSec, sim, assets);
    }
    const float cell = H / 12.0f;
    SDL_SetRenderDrawColor(r, dimmed ? 18 : 30, dimmed ? 22 : 38, dimmed ? 30 : 52, 255);
    for (int gx = 0; !scene && gx <= 16; gx++) {
        SDL_RenderLine(r, gx * cell, 0, gx * cell, H);
    }
    for (int gy = 0; !scene && gy <= 12; gy++) {
        SDL_RenderLine(r, 0, gy * cell, 16 * cell, gy * cell);
    }
    // Real level data, when a .map was loaded (COMET_MAP): draw the
    // parsed records — orientation records as ticks keyed to their
    // quaternion's first two components, object records as markers in
    // a band below, both in file order, 40 per row.
    if (level != nullptr && !dimmed && !scene && !level->records56.empty()) {
        const float step = cell * 0.5f;
        SDL_SetRenderDrawColor(r, 235, 180, 90, 255);
        int i = 0;
        for (const auto& rec : level->records56) {
            const float gx = (float)(i % 40) * step + step;
            const float gy = (float)(i / 40) * step + step;
            SDL_RenderLine(r, gx, gy, gx + rec.quat[0] * step * 0.45f,
                           gy + rec.quat[1] * step * 0.45f);
            ++i;
        }
        const float bandY = (float)(i / 40 + 1) * step + step;
        SDL_SetRenderDrawColor(r, 120, 220, 140, 255);
        int j = 0;
        for (const auto& rec : level->records24) {
            (void)rec;
            const float gx = (float)(j % 40) * step + step;
            const float gy = bandY + (float)(j / 40) * step;
            SDL_FRect dot{gx - 2.0f, gy - 2.0f, 4.0f, 4.0f};
            SDL_RenderFillRect(r, &dot);
            ++j;
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
        if (sim != nullptr) {
            // In-game HUD: two short lines that always fit.
            char buf[128];
            std::snprintf(buf, sizeof(buf), "TIME %d   UNITS %d   KILLS %d   BASE %d/%d",
                          (int)sim->timeSec, (int)sim->entities.size(), sim->kills,
                          sim->baseHits, sim->baseHitsLimit);
            drawText(r, 18, 14.0f, 2.0f, 235, 235, 240, buf);
            std::snprintf(buf, sizeof(buf),
                          "THORIUM %d/%d   SHIP %d%%   SCORE %d/%d   BUILD %d [E]",
                          sim->thorium, sim->thoriumMax,
                          sim->shipAlive ? sim->shipHealth : 0, sim->score,
                          sim->winTarget, sim->buildKind);
            drawText(r, 18, 40.0f, 2.0f, 235, 235, 240, buf);
            if (sim->gameOver) {
                drawTextCentered(r, W * 0.5f, H * 0.42f, 4.0f, 255, 90, 70, "BASE LOST");
            } else if (sim->cleared) {
                drawTextCentered(r, W * 0.5f, H * 0.42f, 4.0f, 120, 255, 140, "WAVE CLEARED");
            }
        } else {
            int title = model.arenaTitleStr();
            if (title >= 0) {
                drawText(r, 18, 14, 3.0f, 240, 200, 90, comet::text::text(title));
            }
            int body = model.arenaBodyStr();
            if (body >= 0) {
                drawBody(r, 18, 58, W * 0.62f, H * 0.6f, comet::text::text(body), 0);
            }
        }
        drawText(r, 18, H - 30.0f, 2.0f, 130, 140, 155,
                 "START: PAUSE    START+BACK: QUIT");
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
            SDL_Log("COMET_MAP %s: %s (%u orient + %u object records)", mapPath,
                    haveLevel ? "loaded" : "FAILED", level.count56(),
                    level.count24());
        }
    }
    const comet::LevelData* levelArg = haveLevel ? &level : nullptr;
    comet::ObjModel objModel;
    bool haveModel = false;
    std::string modelPathStr;
    if (const char* modelPath = SDL_getenv("COMET_MODEL")) {
        modelPathStr = modelPath;
        haveModel = objModel.loadFromFile(modelPath);
        if (logFrames) {
            SDL_Log("COMET_MODEL %s: %s (%zu verts, %zu tris)", modelPath,
                    haveModel ? "loaded" : "FAILED", objModel.positions.size(),
                    objModel.triangles.size());
        }
    }
    const comet::ObjModel* modelArg = haveModel ? &objModel : nullptr;
    SceneAssets sceneAssets;
    if (haveModel) {
        buildSceneAssets(renderer, modelPathStr, sceneAssets);
    }
    comet::ArenaSim sim;
    sim.timeScale = 6.0f; // v1 event pacing (uncalibrated; see arena_sim.h)
    bool simInArena = false;
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

        // Demo script (COMET_START_ARENA=1): synthetic frames walk
        // Title -> Campaign -> Area 1 -> Start Game so the arena (and
        // the 3D scene) can be smoke-tested headlessly.
        InputFrame effFrame = frame;
        if (SDL_getenv("COMET_START_ARENA") != nullptr) {
            if (frameNo < 10) {
                InputFrame demo{};
                switch (frameNo) {
                    case 1: demo.pressed = demo.buttons = BtnStart; break;
                    case 3: demo.moveY = 1.0f; break;
                    case 5: case 7: case 9:
                        demo.pressed = demo.buttons = BtnConfirm; break;
                    default: break;
                }
                effFrame = demo;
            } else if (model.current().id == comet::ScreenId::Arena) {
                // Gameplay demo: patrol + fire so headless runs
                // exercise flight, spawning, and combat.
                InputFrame demo{};
                demo.moveX = ((frameNo / 45) % 2 == 0) ? 0.7f : -0.7f;
                demo.buttons = ((frameNo % 50) < 25) ? BtnAction : 0;
                demo.aimY = -1.0f;
                effFrame = demo;
            }
        }
        model.update(effFrame, (float)winW, (float)winH);
        const comet::MenuAction act = model.takeAction();
        const bool inArenaNow = model.current().id == comet::ScreenId::Arena;
        if (inArenaNow && (!simInArena || act == comet::MenuAction::RestartRun)) {
            sim.reset(levelArg);
        }
        simInArena = inArenaNow;
        if (inArenaNow) {
            sim.update(effFrame, 1.0f / 60.0f);
            if (sim.fxLaser > 0) {
                sfx.play(comet::Sfx::Laser);
                sim.fxLaser = 0;
            }
            if (sim.fxBuild > 0) {
                sfx.play(comet::Sfx::Build);
                sim.fxBuild = 0;
            }
        }

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
            drawArena(renderer, W, H, model, frame, true, levelArg, modelArg,
                        (double)frameNo / 60.0, &sim, sceneAssets);
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
            drawArena(renderer, W, H, model, frame, false, levelArg, modelArg,
                        (double)frameNo / 60.0, &sim, sceneAssets);
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
            if (cur.id == comet::ScreenId::Arena) {
                SDL_Log("sim t=%.0f ents=%d shots=%d base=%d kills=%d spawned=%zu/%d %s",
                        sim.timeSec, (int)sim.entities.size(), (int)sim.shots.size(),
                        sim.baseHits, sim.kills, sim.nextEvent, sim.eventsTotal,
                        sim.gameOver ? "GAMEOVER" : (sim.cleared ? "CLEARED" : ""));
            }
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
