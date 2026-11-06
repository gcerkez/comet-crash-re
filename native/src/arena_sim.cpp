// arena_sim.cpp — see arena_sim.h and notes/RECONSTRUCTION-011.md.

#include "arena_sim.h"

#include <algorithm>
#include <cmath>

namespace comet {
namespace {

constexpr float kShipSpeed = 11.0f;
constexpr float kShotSpeed = 24.0f;
constexpr float kFireInterval = 0.16f;
constexpr float kHitRadius2 = 0.55f * 0.55f;
constexpr float kTowerRange = 4.2f;
constexpr float kTowerInterval = 0.8f;
constexpr float kFactoryInterval = 6.0f;
constexpr int kBuildCost = 25;
constexpr float kBuildTime = 2.5f;

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Factories are kinds 26 and 28 (the branch recovery's factory
// commands pair selectors 4..8 with exactly those types).
bool isFactory(int kind) { return kind == 26 || kind == 28; }

bool isStructureKind(int kind) {
    return (kind >= 20 && kind <= 26) || kind == 28;
}

// The original's per-kind initial (upgrade) level, recovered from
// its map post-constructor transition: a pre-placed structure
// whose record level is at or below this settles at mission
// start; above it, the site waits for construction.
int initialLevel(int kind) {
    if (kind == 25 || kind == 28) return 5;
    if (kind == 26) return 2;
    if (kind >= 20 && kind <= 24) return 1;
    return 0; // not a structure kind
}

// Ground-unit deployment release intervals by produced kind
// (recovered base-deployment timing table, full strength):
// kinds 40..43 stream, 44/46 every 0.5 s, 45/47/48 every 1 s.
float deployInterval(int kind) {
    if (kind == 44 || kind == 46) return 0.5f;
    if (kind == 45 || kind == 47 || kind == 48) return 1.0f;
    return 0.05f;
}

int structureHp(int kind) { return kind >= 26 ? 6 : 4; }

} // namespace

bool ArenaSim::cellOccupied(int x, int y) const {
    for (const SimStructure& s : structures) {
        if ((int)s.x == x && (int)s.y == y) {
            return true;
        }
    }
    return false;
}

void ArenaSim::reset(const LevelData* level) {
    extentCells = 24.0f;
    events_.clear();
    structures.clear();
    if (level != nullptr) {
        extentCells = (float)level->extent;
        events_ = level->events;
        // Pre-placed content: primary records sit at (b2, b3).
        for (const LevelData::Record56& r : level->records56) {
            if (r.type == 0x0B) {
                continue; // the extent marker itself
            }
            SimStructure s;
            s.x = (float)r.b2;
            s.y = (float)r.b3;
            s.kind = r.type;
            s.rock = (r.type == 9);
            // Mission setup (recovered settlement rule): rocks and
            // non-structure records just exist; a structure settles
            // active iff its record level (raw[1]) is at or below
            // its kind's initial level, else it starts dormant.
            if (s.rock || !isStructureKind(r.type)) {
                s.state = 2;
            } else {
                s.state = ((int)r.b1 <= initialLevel(r.type)) ? 2 : 0;
            }
            s.owner = 0;
            s.hp = structureHp(r.type);
            s.producedKind = (r.type == 28) ? 44 : 40;
            structures.push_back(s);
        }
    }
    entities.clear();
    shots.clear();
    thorium = 100;
    winTarget = 25;
    if (level != nullptr) {
        // Header hypotheses (notes 013): [4] = starting Thorium
        // (observed 150..650), [5] = units-to-win (25/30/50).
        const std::uint32_t th = level->header[4];
        if (th >= 50 && th <= 1000) {
            thorium = (int)th;
        }
        const std::uint32_t wt = level->header[5];
        if (wt >= 1 && wt <= 200) {
            winTarget = (int)wt;
        }
    }
    shipX = extentCells * 0.5f;
    shipY = extentCells - 2.0f;
    shipVX = shipVY = 0.0f;
    shipYaw = 0.0f;
    timeSec = 0.0f;
    nextEvent = 0;
    eventsTotal = (int)events_.size();
    // (thorium may be overridden by the header hypothesis above;
    // reset it before that block runs — see reset order note)
    buildKind = 21;
    shipHealth = 100;
    shipAlive = true;
    respawnTimer = 0.0f;
    deaths = 0;
    score = 0;
    meteorTimer_ = 5.0f;
    baseHits = 0;
    kills = 0;
    gameOver = false;
    cleared = false;
    fireCooldown_ = 0.0f;
    spawnCounter_ = 0;
    fxLaser = 0;
    fxBuild = 0;
}

void ArenaSim::fireShot(float x, float y, float dx, float dy, bool fromPlayer) {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f) {
        return;
    }
    SimShot s;
    s.x = x;
    s.y = y;
    s.vx = dx / len * kShotSpeed;
    s.vy = dy / len * kShotSpeed;
    s.fromPlayer = fromPlayer;
    shots.push_back(s);
    ++fxLaser;
}

void ArenaSim::activateNextStructure(int kind) {
    for (SimStructure& s : structures) {
        if (!s.rock && s.owner == 0 && s.kind == kind && s.state == 0) {
            s.state = 1; // construction starts; completes on its timer
            s.timer = kBuildTime;
            return;
        }
    }
    // No dormant structure of this kind: the script sends a
    // meteor across the field instead (keeps sparse levels lively
    // and feeds the Thorium economy).
    (void)kind;
    spawnMeteor();
}

void ArenaSim::spawnMeteor() {
    ++spawnCounter_;
    SimEntity e;
    e.kind = 90; // meteoroid: the Thorium source
    e.ground = false;
    const float row = (float)((spawnCounter_ * 13u) % (unsigned)extentCells);
    e.y = 1.0f + row * 0.8f;
    if (spawnCounter_ % 2u == 0u) {
        e.x = -0.5f;
        e.vx = 3.0f;
    } else {
        e.x = extentCells + 0.5f;
        e.vx = -3.0f;
    }
    e.hp = 1;
    entities.push_back(e);
}

void ArenaSim::destroyShip() {
    ++deaths;
    shipAlive = false;
    respawnTimer = 2.0f; // recovered reset countdown (C0 = 2)
    fxLaser += 2;        // destruction burst (sound/particles in the shell later)
}

void ArenaSim::update(const input::InputFrame& in, float dt) {
    if (gameOver || cleared || dt <= 0.0f) {
        return;
    }
    timeSec += dt * timeScale;

    if (!shipAlive) {
        respawnTimer -= dt;
        if (respawnTimer <= 0.0f) {
            shipAlive = true;
            shipHealth = 100;
            shipX = extentCells * 0.5f;
            shipY = extentCells - 2.0f;
        }
    }
    meteorTimer_ -= dt;
    if (meteorTimer_ <= 0.0f) {
        meteorTimer_ = 7.0f;
        spawnMeteor();
    }

    // --- ship flight
    shipVX = shipAlive ? in.moveX * kShipSpeed : 0.0f;
    shipVY = shipAlive ? in.moveY * kShipSpeed : 0.0f;
    shipX = clampf(shipX + shipVX * dt, 0.6f, extentCells - 0.6f);
    shipY = clampf(shipY + shipVY * dt, 0.6f, extentCells - 0.6f);
    if (in.moveX != 0.0f || in.moveY != 0.0f) {
        shipYaw = std::atan2(in.moveX, -in.moveY);
    } else if (in.aimX != 0.0f || in.aimY != 0.0f) {
        shipYaw = std::atan2(in.aimX, -in.aimY);
    }

    // --- build-kind cycling + construction (funded request:
    // pay up front, structure completes after its build timer).
    if ((in.pressed & input::BtnAlt) != 0) {
        // Buildable kinds: the weapons, Gateway, and both Ops.
        buildKind = (buildKind == 26) ? 28 : (buildKind >= 28 ? 21 : buildKind + 1);
    }
    if (shipAlive && (in.pressed & input::BtnConfirm) != 0 && thorium >= kBuildCost) {
        const int cx = (int)std::floor(shipX);
        const int cy = (int)std::floor(shipY);
        if (!cellOccupied(cx, cy)) {
            thorium -= kBuildCost;
            SimStructure s;
            s.x = (float)cx;
            s.y = (float)cy;
            s.kind = buildKind;
            s.state = 1; // building
            s.owner = 1;
            s.hp = structureHp(buildKind);
            s.timer = kBuildTime;
            structures.push_back(s);
            ++fxBuild;
        }
    }

    // --- ship fire
    fireCooldown_ -= dt;
    if (shipAlive && (in.buttons & input::BtnAction) != 0 && fireCooldown_ <= 0.0f) {
        fireCooldown_ = kFireInterval;
        float dx = in.aimX, dy = in.aimY;
        if (dx == 0.0f && dy == 0.0f) {
            dx = std::sin(shipYaw);
            dy = -std::cos(shipYaw);
        }
        fireShot(shipX, shipY, dx, dy, true);
    }

    // --- the level's timed script: activate structures by kind
    while (nextEvent < events_.size() && events_[nextEvent].value <= timeSec) {
        activateNextStructure(events_[nextEvent].bytes[3]);
        ++nextEvent;
    }

    // --- structures: build completion, factory production, towers
    for (SimStructure& s : structures) {
        if (s.rock) {
            continue;
        }
        if (s.state == 1) { // building
            s.timer -= dt;
            if (s.timer <= 0.0f) {
                s.state = 2;
                s.timer = 0.0f;
                if (s.kind == 28) {
                    s.producedKind = 44;
                } else if (s.kind == 26) {
                    s.producedKind = 40;
                }
            }
            continue;
        }
        if (s.state != 2) {
            continue;
        }
        s.timer -= dt;
        if (isFactory(s.kind)) {
            // Production stockpiles pending orders; deployment
            // releases them from the factory cell on the recovered
            // per-kind intervals. (The original transports output
            // to the owner's base first — not yet modeled.)
            if (s.timer <= 0.0f) {
                s.timer = kFactoryInterval;
                if (s.pending < 8) {
                    ++s.pending;
                }
            }
            s.deployTimer -= dt;
            if (s.pending > 0 && s.deployTimer <= 0.0f) {
                s.deployTimer = deployInterval(s.producedKind);
                --s.pending;
                SimEntity e;
                e.kind = s.producedKind;
                const int bandHi = (s.kind == 28) ? 47 : 43;
                const int bandLo = (s.kind == 28) ? 44 : 40;
                s.producedKind = s.producedKind >= bandHi ? bandLo : s.producedKind + 1;
                e.x = s.x + 0.5f;
                e.y = s.y + (s.owner == 1 ? -0.5f : 1.5f);
                e.speed = 0.9f;
                e.hp = 2;
                e.ground = true;
                e.playerOwned = (s.owner == 1);
                entities.push_back(e);
            }
        } else if (!isFactory(s.kind) && s.timer <= 0.0f) {
            // Tower: fire at the nearest opposing target in range.
            float bestD2 = kTowerRange * kTowerRange;
            float tx = 0, ty = 0;
            bool have = false;
            if (s.owner == 0) {
                if (shipAlive) {
                    const float dx = shipX - s.x, dy = shipY - s.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) {
                        bestD2 = d2;
                        tx = shipX;
                        ty = shipY;
                        have = true;
                    }
                }
                if (!have) {
                    for (const SimEntity& e : entities) {
                        if (!e.alive || !e.playerOwned) {
                            continue;
                        }
                        const float dx = e.x - s.x, dy = e.y - s.y;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 < bestD2) {
                            bestD2 = d2;
                            tx = e.x;
                            ty = e.y;
                            have = true;
                        }
                    }
                }
            } else {
                for (const SimEntity& e : entities) {
                    if (!e.alive) {
                        continue;
                    }
                    const float dx = e.x - s.x, dy = e.y - s.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) {
                        bestD2 = d2;
                        tx = e.x;
                        ty = e.y;
                        have = true;
                    }
                }
            }
            if (have) {
                s.timer = kTowerInterval;
                fireShot(s.x, s.y, tx - s.x, ty - s.y, s.owner == 1);
            } else {
                s.timer = 0.15f; // re-scan soon
            }
        }
    }

    // --- entity advance: ground units head for the base edge
    // (enemy) or the far edge (player-produced); comets dive.
    for (SimEntity& e : entities) {
        if (!e.alive) {
            continue;
        }
        if (e.kind == 90) { // meteor: straight drift, leaves the field
            e.x += e.vx * dt;
            if (e.x < -1.5f || e.x > extentCells + 1.5f) {
                e.alive = false;
            }
            continue;
        }
        const float dir = e.playerOwned ? -1.0f : 1.0f;
        const float nx = e.x;
        const float ny = e.y + dir * e.speed * dt;
        // Rocks block: sidestep around a blocking cell.
        if (e.ground && cellOccupied((int)std::floor(nx), (int)std::floor(ny)) &&
            !cellOccupied((int)std::floor(nx + 1.0f), (int)std::floor(ny))) {
            e.x = nx + e.speed * dt;
        } else if (e.ground && cellOccupied((int)std::floor(nx), (int)std::floor(ny))) {
            // fully blocked this frame: wait
        } else {
            e.x = nx;
            e.y = ny;
        }
        if (!e.playerOwned && e.y >= extentCells - 0.4f) {
            e.alive = false;
            ++baseHits;
            if (baseHits >= baseHitsLimit) {
                gameOver = true;
            }
        }
        if (e.playerOwned && e.y <= 0.4f) {
            e.alive = false;
            ++score;
            if (score >= winTarget) {
                cleared = true;
            }
        }
        // Contact with the ship damages both.
        if (shipAlive && e.alive && e.kind != 90 && !e.playerOwned) {
            const float dx = e.x - shipX, dy = e.y - shipY;
            if (dx * dx + dy * dy <= 0.55f * 0.55f) {
                e.alive = false;
                shipHealth -= 8;
                if (shipHealth <= 0) {
                    destroyShip();
                }
            }
        }
    }

    // --- shots advance + hits (units, structures, rocks absorb)
    for (SimShot& s : shots) {
        if (!s.alive) {
            continue;
        }
        s.x += s.vx * dt;
        s.y += s.vy * dt;
        if (s.x < -1.0f || s.x > extentCells + 1.0f || s.y < -1.0f ||
            s.y > extentCells + 1.0f) {
            s.alive = false;
            continue;
        }
        for (SimEntity& e : entities) {
            if (!e.alive) {
                continue;
            }
            const float dx = e.x - s.x, dy = e.y - s.y;
            if (dx * dx + dy * dy <= kHitRadius2) {
                s.alive = false;
                if (--e.hp <= 0) {
                    e.alive = false;
                    ++kills;
                    if (e.kind == 90) {
                        thorium += 20; // smashed meteoroid releases Thorium
                    }
                    ++fxLaser;
                }
                break;
            }
        }
        if (!s.alive) {
            continue;
        }
        if (!s.fromPlayer && shipAlive) {
            const float dx = shipX - s.x, dy = shipY - s.y;
            if (dx * dx + dy * dy <= 0.6f * 0.6f) {
                s.alive = false;
                shipHealth -= 10;
                if (shipHealth <= 0) {
                    destroyShip();
                }
                continue;
            }
        }
        for (SimStructure& st : structures) {
            if (st.rock || st.state == 0) {
                if (st.rock) {
                    const float dx = (st.x + 0.5f) - s.x, dy = (st.y + 0.5f) - s.y;
                    if (dx * dx + dy * dy <= kHitRadius2) {
                        s.alive = false;
                    }
                }
                continue;
            }
            if ((st.owner == 1) == s.fromPlayer) {
                continue; // no friendly fire
            }
            const float dx = (st.x + 0.5f) - s.x, dy = (st.y + 0.5f) - s.y;
            if (dx * dx + dy * dy <= kHitRadius2) {
                s.alive = false;
                if (--st.hp <= 0) {
                    st.state = 0;      // destroyed: back to dormant slot
                    st.owner = 0;
                    thorium += s.fromPlayer ? 10 : 0;
                    ++kills;
                }
                break;
            }
        }
    }

    entities.erase(std::remove_if(entities.begin(), entities.end(),
                                  [](const SimEntity& e) { return !e.alive; }),
                   entities.end());
    shots.erase(std::remove_if(shots.begin(), shots.end(),
                               [](const SimShot& s) { return !s.alive; }),
                shots.end());

    if (nextEvent >= events_.size() && entities.empty() && eventsTotal > 0) {
        bool anyActiveEnemy = false;
        for (const SimStructure& s : structures) {
            if (!s.rock && s.owner == 0 && s.state == 2 && isFactory(s.kind)) {
                anyActiveEnemy = true;
            }
        }
        if (!anyActiveEnemy) {
            cleared = true;
        }
    }
}

} // namespace comet
