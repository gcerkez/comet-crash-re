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

// One named model from the game's registry, with its texture.
struct MeshSlot {
    comet::ObjModel model;
    SDL_Texture* tex = nullptr;
    bool ok = false;
    float span = 1.0f; // approx. world size before scaling (2*radius)
};

// The game's artwork, resolved from the model directory at start.
struct SceneAssets {
    comet::ObjModel scout;
    bool haveScout = false;
    SDL_Texture* shipTex = nullptr;
    SDL_Texture* scoutTex = nullptr;
    SDL_Texture* groundTex = nullptr;
    SDL_Texture* rockTex = nullptr;
    SDL_Texture* structTex[29] = {}; // by structure kind (billboard fallback)
    // The full registry (base-game package), composed as meshes.
    MeshSlot terrain[4]; // gaseous, ice, lava, rock (header world idx)
    MeshSlot turretStand, turretBarrel, laserStand, laserBarrel;
    MeshSlot missileStand, missileBarrels, barracksBasic, barracksAdv;
    MeshSlot baseShell, tesla, gateway, beaconLamp, platBasic;
    MeshSlot geode, rockA, beetle, flyer, scoutMesh;
    MeshSlot agentMedic, agentCarrier, agentLaser;
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
            // Some .mtl files carry absolute paths from the
            // developer's machine (/home/jbates/pelfast/...);
            // only the file name is meaningful here.
            const std::size_t slash = name.find_last_of("/\\");
            if (slash != std::string::npos) {
                name = name.substr(slash + 1);
            }
            if (!name.empty()) {
                found = dirOf(objPath) + "/" + name;
                // Several .mtl files name .tif sources; the shipped
                // textures are the same stems as .dds.
                std::FILE* probe = std::fopen(found.c_str(), "rb");
                if (probe == nullptr && found.size() > 4 &&
                    found.substr(found.size() - 4) == ".tif") {
                    found = found.substr(0, found.size() - 4) + ".dds";
                } else if (probe != nullptr) {
                    std::fclose(probe);
                }
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
    // The full model registry from the base-game package.
    auto loadMesh = [&r, &dataRoot](MeshSlot& slot, const char* rel) {
        const std::string path = dataRoot + rel;
        if (slot.model.loadFromFile(path.c_str())) {
            slot.ok = true;
            slot.span = slot.model.radius > 0 ? 2.0f * slot.model.radius : 1.0f;
            const std::string tp = mtlTexturePath(path);
            if (!tp.empty()) {
                slot.tex = loadDdsTexture(r, tp);
            }
        }
    };
    loadMesh(A.terrain[0], "/models/care/cometGaseous/cometGaseous.obj");
    loadMesh(A.terrain[1], "/models/care/cometIce/cometIce.obj");
    loadMesh(A.terrain[2], "/models/care/cometLava/lavaComet.obj");
    loadMesh(A.terrain[3], "/models/care/cometRock_03/cometRock.obj");
    loadMesh(A.turretStand, "/models/care/weapons/structTurretStand.obj");
    loadMesh(A.turretBarrel, "/models/care/weapons/structTurretBarrel.obj");
    loadMesh(A.laserStand, "/models/care/weapons/structLaserStand.obj");
    loadMesh(A.laserBarrel, "/models/care/weapons/structLaserBarrel.obj");
    loadMesh(A.missileStand, "/models/care/weapons/structMissileStand.obj");
    loadMesh(A.missileBarrels, "/models/care/weapons/structMissileBarrels.obj");
    loadMesh(A.barracksBasic, "/models/care/structures/structBarracksBasic.obj");
    loadMesh(A.barracksAdv, "/models/care/structures/structBarracksAdv.obj");
    loadMesh(A.baseShell, "/models/structBaseShell.obj");
    loadMesh(A.tesla, "/models/structTesla.obj");
    loadMesh(A.gateway, "/models/gateway.obj");
    loadMesh(A.beaconLamp, "/models/care/structures/structBeaconLamp.obj");
    loadMesh(A.platBasic, "/models/care/platforms/platBasic.obj");
    loadMesh(A.geode, "/models/care/resourceGeode/resourceGeode.obj");
    loadMesh(A.rockA, "/models/care/rockA.obj");
    loadMesh(A.beetle, "/models/care/agents/beetle.obj");
    loadMesh(A.flyer, "/models/care/agents/flyer.obj");
    loadMesh(A.scoutMesh, "/models/care/scout02.obj");
    loadMesh(A.agentMedic, "/models/care/agents/agentAdv_Medic.obj");
    loadMesh(A.agentCarrier, "/models/care/agents/agentAdv_Carrier.obj");
    loadMesh(A.agentLaser, "/models/care/agents/agentAdv_Laser.obj");
    SDL_Log("scene assets: shipTex=%d scout=%d groundTex=%d terrain=%d turret=%d geode=%d",
            A.shipTex != nullptr, (int)A.haveScout, A.groundTex != nullptr,
            (int)A.terrain[0].ok, (int)A.turretStand.ok, (int)A.geode.ok);
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
    // vertex color = tint x shade). Rotation is a yaw, or a full
    // quaternion (x,y,z,w) when quat is given — level records
    // orient placed content with quaternions.
    void addModel(SDL_Texture* tex, const Camera& cam, float W, float H,
                  const comet::ObjModel& obj, float wx, float wy, float wz,
                  float yaw, const float* quat, float scale, SDL_FColor tint) {
        float R[3][3];
        if (quat != nullptr) {
            const float x = quat[0], y = quat[1], z = quat[2], w = quat[3];
            R[0][0] = 1 - 2 * (y * y + z * z);
            R[0][1] = 2 * (x * y - z * w);
            R[0][2] = 2 * (x * z + y * w);
            R[1][0] = 2 * (x * y + z * w);
            R[1][1] = 1 - 2 * (x * x + z * z);
            R[1][2] = 2 * (y * z - x * w);
            R[2][0] = 2 * (x * z - y * w);
            R[2][1] = 2 * (y * z + x * w);
            R[2][2] = 1 - 2 * (x * x + y * y);
        } else {
            const float cs = std::cos(yaw), sn = std::sin(yaw);
            R[0][0] = cs;  R[0][1] = 0; R[0][2] = -sn;
            R[1][0] = 0;   R[1][1] = 1; R[1][2] = 0;
            R[2][0] = sn;  R[2][1] = 0; R[2][2] = cs;
        }
        const Vec3f light = vnorm({0.5f, 0.8f, 0.3f});
        for (std::size_t ti = 0; ti < obj.triangles.size(); ++ti) {
            const auto& tr = obj.triangles[ti];
            Vec3f wp[3];
            SDL_FPoint uv[3];
            for (int k = 0; k < 3; ++k) {
                const auto& p = obj.positions[tr.v[k]];
                wp[k] = {(p[0] * R[0][0] + p[1] * R[0][1] + p[2] * R[0][2]) * scale + wx,
                         (p[0] * R[1][0] + p[1] * R[1][1] + p[2] * R[1][2]) * scale + wy,
                         (p[0] * R[2][0] + p[1] * R[2][1] + p[2] * R[2][2]) * scale + wz};
                if (tex != nullptr && tr.t[k] >= 0 &&
                    tr.t[k] < (int)obj.uvs.size()) {
                    const auto& t2 = obj.uvs[tr.t[k]];
                    uv[k] = {t2[0], 1.0f - t2[1]}; // DDS rows are top-first
                } else {
                    uv[k] = {0, 0};
                }
            }
            const auto& fn = obj.faceNormals[ti];
            const Vec3f n{fn[0] * R[0][0] + fn[1] * R[0][1] + fn[2] * R[0][2],
                          fn[0] * R[1][0] + fn[1] * R[1][1] + fn[2] * R[1][2],
                          fn[0] * R[2][0] + fn[1] * R[2][1] + fn[2] * R[2][2]};
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
    // World scale: the comet terrains span ~36 native units for a
    // 24-cell arena, so 1 cell = 1.5 world units. (Constant to be
    // confirmed against the original's render code; the registry
    // struct hunt is noted in RECONSTRUCTION-018.)
    const float WPC = 1.5f;
    int extentCells = 24;
    if (level != nullptr) {
        extentCells = level->extent;
    }
    if (sim != nullptr) {
        extentCells = sim->extentCells;
    }
    const float halfWorld = extentCells * 0.5f * WPC;
    const Camera cam = makeCamera(W, H, halfWorld);
    TriSink sink;
    auto toWorld = [&](float simCoord) { return (simCoord - extentCells * 0.5f) * WPC; };

    // Terrain: the comet's own sculpted mesh (chosen by the level
    // header's world index), textured with its 2048px surface.
    const MeshSlot* terrain = &A.terrain[0];
    if (level != nullptr) {
        const unsigned world = (level->header[2] >> 16) & 0xFFFFu;
        if (world < 4 && A.terrain[world].ok) {
            terrain = &A.terrain[world];
        }
    }
    if (terrain->ok) {
        const float sc = (extentCells * WPC) / terrain->span;
        sink.addModel(terrain->tex, cam, W, H, terrain->model, 0, -0.05f, 0,
                      0.0f, nullptr, sc, {1, 1, 1, 1});
    } else {
        // Fallback: flat dark field.
        sink.addQuad(nullptr, cam, W, H, {-halfWorld, 0, -halfWorld},
                     {halfWorld, 0, -halfWorld}, {halfWorld, 0, halfWorld},
                     {-halfWorld, 0, halfWorld}, {0.05f, 0.07f, 0.11f, 1.0f});
    }

    if (sim != nullptr) {
        // Base pads + the base shell mesh.
        auto baseAt = [&](float bx, float bz, bool player) {
            const float cx = toWorld(bx), cz = toWorld(bz);
            const SDL_FColor col = player ? SDL_FColor{0.10f, 0.45f, 0.42f, 1}
                                          : SDL_FColor{0.50f, 0.14f, 0.10f, 1};
            sink.addQuad(nullptr, cam, W, H, {cx - 1.1f, 0.02f, cz - 1.1f},
                         {cx + 1.1f, 0.02f, cz - 1.1f},
                         {cx + 1.1f, 0.02f, cz + 1.1f},
                         {cx - 1.1f, 0.02f, cz + 1.1f}, col);
            if (A.baseShell.ok) {
                const float sc = 2.3f / A.baseShell.span;
                // The shell's shipped texture reference is a
                // developer-path team-lines overlay; the shell is
                // presented in team colour.
                const SDL_FColor tint = player
                                            ? SDL_FColor{0.45f, 0.95f, 0.88f, 1}
                                            : SDL_FColor{1.0f, 0.52f, 0.38f, 1};
                sink.addModel(A.baseShell.tex, cam, W, H, A.baseShell.model,
                              cx, 0.0f, cz, 0.0f, nullptr, sc, tint);
            }
        };
        baseAt(sim->playerBaseX, sim->playerBaseZ, true);
        baseAt(sim->enemyBaseX, sim->enemyBaseZ, false);

        // Placed content: real meshes, composed as the registry
        // names them (stand + barrel parts, platforms under sites).
        for (const auto& s : sim->structures) {
            const float cx = toWorld(s.x + 0.5f), cz = toWorld(s.y + 0.5f);
            if (s.rock) {
                if (A.rockA.ok) {
                    const float sc = 1.5f / A.rockA.span;
                    sink.addModel(A.rockA.tex, cam, W, H, A.rockA.model, cx,
                                  0.0f, cz, 0.0f, s.quat, sc,
                                  {0.85f, 0.82f, 0.78f, 1.0f});
                }
                continue;
            }
            if (s.kind < 20 || s.kind > 28) {
                continue;
            }
            SDL_FColor col;
            if (s.state == 0) {
                col = {0.45f, 0.45f, 0.52f, 1.0f};
            } else if (s.state == 1) {
                const float p = 0.8f + 0.2f * std::sin((float)t * 6.0f);
                col = {p, p, p, 1.0f};
            } else {
                col = s.owner == 1 ? SDL_FColor{0.82f, 1.0f, 0.97f, 1.0f}
                                   : SDL_FColor{1.0f, 0.84f, 0.72f, 1.0f};
            }
            // Every site sits on a platform (platBasic/platAdv).
            if (A.platBasic.ok) {
                const float sc = 1.5f / A.platBasic.span;
                sink.addModel(A.platBasic.tex, cam, W, H, A.platBasic.model,
                              cx, 0.0f, cz, 0.0f, nullptr, sc, col);
            }
            if (s.state == 0) {
                continue; // dormant: just the platform
            }
            auto part = [&](const MeshSlot& slot, float y, float targetSpan) {
                if (!slot.ok) {
                    return;
                }
                const float sc = targetSpan / slot.span;
                sink.addModel(slot.tex, cam, W, H, slot.model, cx, y, cz, 0.0f,
                              nullptr, sc, col);
            };
            switch (s.kind) {
                case 20: part(A.baseShell, 0, 2.0f); break;
                case 21:
                    part(A.turretStand, 0.1f, 1.3f);
                    part(A.turretBarrel, 0.85f, 1.3f);
                    break;
                case 22: part(A.tesla, 0.1f, 1.3f); break;
                case 23:
                    part(A.laserStand, 0.1f, 1.3f);
                    part(A.laserBarrel, 0.85f, 1.3f);
                    break;
                case 24:
                    part(A.missileStand, 0.1f, 1.3f);
                    part(A.missileBarrels, 0.75f, 1.3f);
                    break;
                case 25: part(A.gateway, 0.1f, 1.4f); break;
                case 26: part(A.barracksBasic, 0.1f, 1.45f); break;
                case 27: part(A.beaconLamp, 0.1f, 1.1f); break;
                case 28: part(A.barracksAdv, 0.1f, 1.45f); break;
                default: break;
            }
        }

        // Units: their own meshes (ground kinds low, air kinds up).
        for (const auto& e : sim->entities) {
            const float wx = toWorld(e.x), wz = toWorld(e.y);
            if (e.kind == 90) { // a flying Thorium geode
                if (A.geode.ok) {
                    const float sc = 1.05f / A.geode.span;
                    sink.addModel(A.geode.tex, cam, W, H, A.geode.model, wx,
                                  0.8f, wz, (float)t * 1.5f + e.x, nullptr, sc,
                                  {1, 1, 1, 1});
                }
                continue;
            }
            const SDL_FColor tint = e.playerOwned
                                        ? SDL_FColor{0.78f, 1.0f, 0.97f, 1.0f}
                                        : SDL_FColor{1.0f, 0.84f, 0.72f, 1.0f};
            const MeshSlot* slot = &A.scoutMesh;
            bool air = false;
            switch (e.kind) {
                case 40: slot = &A.scoutMesh; break;
                case 41: slot = &A.beetle; break;
                case 42: slot = &A.flyer; air = true; break;
                case 43: slot = &A.flyer; air = true; break;
                case 44: slot = &A.beetle; break;
                case 45: slot = &A.agentMedic; break;
                case 46: slot = &A.agentLaser; break;
                case 47: slot = &A.agentCarrier; air = true; break;
                default: slot = &A.beetle; break;
            }
            float yaw = e.playerOwned ? -1.5708f : 1.5708f;
            if (e.pathIdx < (int)e.path.size()) {
                const float dx = e.path[e.pathIdx].first + 0.5f - e.x;
                const float dz = e.path[e.pathIdx].second + 0.5f - e.y;
                if (dx * dx + dz * dz > 0.0001f) {
                    yaw = std::atan2(dz, dx);
                }
            }
            if (slot->ok) {
                const float sc = 1.2f / slot->span;
                sink.addModel(slot->tex, cam, W, H, slot->model, wx,
                              air ? 0.9f : 0.10f, wz, yaw, nullptr, sc, tint);
            } else if (A.haveScout) {
                const float sc = 0.5f / A.scout.radius;
                sink.addModel(A.scoutTex, cam, W, H, A.scout, wx, 0.3f, wz,
                              yaw, nullptr, sc, tint);
            }
        }

        // The player's ship (COMET_MODEL: playerShip.obj).
        if (sim->shipAlive && !obj.empty()) {
            const float sc = obj.radius > 0 ? 1.5f / (2.0f * obj.radius) : 1.0f;
            sink.addModel(A.shipTex, cam, W, H, obj, toWorld(sim->shipX),
                          0.7f, toWorld(sim->shipY), sim->shipYaw, nullptr, sc,
                          {1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    sink.flush(r);

    // Shots on top.
    if (sim != nullptr) {
        SDL_SetRenderDrawColor(r, 255, 230, 150, 255);
        for (const auto& s2 : sim->shots) {
            float sx, sy, dep;
            if (project(cam, W, H,
                        {toWorld(s2.x), 0.9f, toWorld(s2.y)}, sx, sy, dep)) {
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
        if (const char* shotPath = SDL_getenv("COMET_SCREENSHOT")) {
            if (maxFrames > 0 && frameNo == maxFrames - 1) {
                if (SDL_Surface* surf = SDL_RenderReadPixels(renderer, nullptr)) {
                    SDL_SaveBMP(surf, shotPath);
                    SDL_DestroySurface(surf);
                }
            }
        }
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
