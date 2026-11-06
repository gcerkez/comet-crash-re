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

// Kinds that produce ground units once active (v1: 24; the
// original's factory family — see the branch recovery docs).
bool isFactory(int kind) { return kind == 24; }

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
            s.state = s.rock ? 2 : 0; // rocks just exist; structures start dormant
            s.owner = 0;
            s.hp = structureHp(r.type);
            structures.push_back(s);
        }
    }
    entities.clear();
    shots.clear();
    shipX = extentCells * 0.5f;
    shipY = extentCells - 2.0f;
    shipVX = shipVY = 0.0f;
    shipYaw = 0.0f;
    timeSec = 0.0f;
    nextEvent = 0;
    eventsTotal = (int)events_.size();
    thorium = 100;
    buildKind = 21;
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
            s.state = 2;
            s.timer = 0.0f;
            return;
        }
    }
    // No dormant structure of this kind: the script sends a comet
    // unit from the far edge instead (keeps sparse levels lively).
    SimEntity e;
    e.kind = kind;
    ++spawnCounter_;
    const float spread = (float)((spawnCounter_ * 37u) % 89u) / 89.0f;
    e.x = 1.0f + spread * (extentCells - 2.0f);
    e.y = 0.6f;
    e.speed = 1.1f + (float)(kind % 5) * 0.18f;
    e.hp = kind >= 26 ? 2 : 1;
    e.ground = false;
    entities.push_back(e);
}

void ArenaSim::update(const input::InputFrame& in, float dt) {
    if (gameOver || cleared || dt <= 0.0f) {
        return;
    }
    timeSec += dt * timeScale;

    // --- ship flight
    shipVX = in.moveX * kShipSpeed;
    shipVY = in.moveY * kShipSpeed;
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
        buildKind = buildKind >= 26 ? 21 : buildKind + 1;
    }
    if ((in.pressed & input::BtnConfirm) != 0 && thorium >= kBuildCost) {
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
    if ((in.buttons & input::BtnAction) != 0 && fireCooldown_ <= 0.0f) {
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
            }
            continue;
        }
        if (s.state != 2) {
            continue;
        }
        s.timer -= dt;
        if (isFactory(s.kind) && s.timer <= 0.0f) {
            s.timer = kFactoryInterval;
            SimEntity e;
            e.kind = 40; // ground unit kind family (40..47)
            e.x = s.x;
            e.y = s.y + (s.owner == 1 ? -1.0f : 1.0f);
            e.speed = 0.9f;
            e.hp = 2;
            e.ground = true;
            entities.push_back(e);
        } else if (!isFactory(s.kind) && s.timer <= 0.0f) {
            // Tower: fire at the nearest opposing target in range.
            float bestD2 = kTowerRange * kTowerRange;
            float tx = 0, ty = 0;
            bool have = false;
            if (s.owner == 0) {
                const float dx = shipX - s.x, dy = shipY - s.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < bestD2) {
                    bestD2 = d2;
                    tx = shipX;
                    ty = shipY;
                    have = true;
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
        const float dir = e.ground ? 1.0f : 1.0f;
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
        if (e.y >= extentCells - 0.4f) {
            e.alive = false;
            ++baseHits;
            if (baseHits >= baseHitsLimit) {
                gameOver = true;
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
                    thorium += 5;
                    ++fxLaser;
                }
                break;
            }
        }
        if (!s.alive) {
            continue;
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
