// arena_sim.cpp — see arena_sim.h.

#include "arena_sim.h"

#include <algorithm>
#include <cmath>

namespace comet {
namespace {

// v1 tuning (to be replaced by recovered values as the record
// consumers are identified in the corpus):
constexpr float kShipSpeed = 11.0f;   // cells/second at full tilt
constexpr float kShotSpeed = 24.0f;   // cells/second
constexpr float kFireInterval = 0.16f;
constexpr float kHitRadius2 = 0.55f * 0.55f;

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace

void ArenaSim::reset(const LevelData* level) {
    extentCells = 24.0f;
    events_.clear();
    if (level != nullptr) {
        extentCells = (float)level->extent;
        events_ = level->events;
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
    baseHits = 0;
    kills = 0;
    gameOver = false;
    cleared = false;
    fireCooldown_ = 0.0f;
    spawnCounter_ = 0;
    fxLaser = 0;
}

void ArenaSim::update(const input::InputFrame& in, float dt) {
    if (gameOver || cleared || dt <= 0.0f) {
        return;
    }
    timeSec += dt * timeScale;

    // --- ship flight: velocity tracks the move vector directly
    // (the pad decoder's normalized axes), clamped to the arena.
    shipVX = in.moveX * kShipSpeed;
    shipVY = in.moveY * kShipSpeed;
    shipX = clampf(shipX + shipVX * dt, 0.6f, extentCells - 0.6f);
    shipY = clampf(shipY + shipVY * dt, 0.6f, extentCells - 0.6f);
    if (in.moveX != 0.0f || in.moveY != 0.0f) {
        shipYaw = std::atan2(in.moveX, -in.moveY);
    } else if (in.aimX != 0.0f || in.aimY != 0.0f) {
        shipYaw = std::atan2(in.aimX, -in.aimY);
    }

    // --- firing: Attack button, toward the aim vector when present,
    // otherwise toward the spawn edge (-y).
    fireCooldown_ -= dt;
    if ((in.buttons & input::BtnAction) != 0 && fireCooldown_ <= 0.0f) {
        fireCooldown_ = kFireInterval;
        float dx = in.aimX, dy = in.aimY;
        if (dx == 0.0f && dy == 0.0f) {
            dx = std::sin(shipYaw);
            dy = -std::cos(shipYaw);
        }
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.0f) {
            dx /= len;
            dy /= len;
        }
        SimShot s;
        s.x = shipX;
        s.y = shipY;
        s.vx = dx * kShotSpeed;
        s.vy = dy * kShotSpeed;
        shots.push_back(s);
        ++fxLaser;
    }

    // --- scheduled spawns: the level's own event list. The float
    // key is the schedule time (monotonic in file order). Spawn x
    // spreads deterministically across the far edge in v1.
    while (nextEvent < events_.size() && events_[nextEvent].value <= timeSec) {
        const LevelData::Record24& ev = events_[nextEvent];
        SimEntity e;
        e.kind = ev.bytes[3]; // opcode
        ++spawnCounter_;
        const float spread = (float)((spawnCounter_ * 37u) % 89u) / 89.0f;
        e.x = 1.0f + spread * (extentCells - 2.0f);
        e.y = 0.6f;
        e.speed = 1.1f + (float)(e.kind % 5) * 0.18f;
        e.hp = e.kind >= 26 ? 2 : 1;
        entities.push_back(e);
        ++nextEvent;
    }

    // --- entity advance + base contact
    for (SimEntity& e : entities) {
        if (!e.alive) {
            continue;
        }
        e.y += e.speed * dt;
        if (e.y >= extentCells - 0.4f) {
            e.alive = false;
            ++baseHits;
            if (baseHits >= baseHitsLimit) {
                gameOver = true;
            }
        }
    }

    // --- shots advance + hits
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
                    ++fxLaser;
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
        cleared = true;
    }
}

} // namespace comet
