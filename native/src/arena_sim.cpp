// arena_sim.cpp — see arena_sim.h and notes/RECONSTRUCTION-011.md.

#include "arena_sim.h"

#include <algorithm>
#include <cmath>
#include <queue>

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
            if (r.type == 0x0B || r.type == 10 || r.type == 29) {
                continue; // extent and base markers are not structures
            }
            SimStructure s;
            s.quat[0] = r.quat[0];
            s.quat[1] = r.quat[1];
            s.quat[2] = r.quat[2];
            s.quat[3] = r.quat[3];
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
    playerBaseX = extentCells * 0.5f;
    playerBaseZ = extentCells - 0.5f;
    enemyBaseX = extentCells * 0.5f;
    enemyBaseZ = 0.5f;
    if (level != nullptr) {
        if (level->hasPlayerBase) {
            playerBaseX = (float)level->playerBaseX + 0.5f;
            playerBaseZ = (float)level->playerBaseZ + 0.5f;
        }
        if (level->hasEnemyBase) {
            enemyBaseX = (float)level->enemyBaseX + 0.5f;
            enemyBaseZ = (float)level->enemyBaseZ + 0.5f;
        }
        rockEvents_ = level->rockEvents;
        std::stable_sort(rockEvents_.begin(), rockEvents_.end(),
                         [](const LevelData::RockEvent& a,
                            const LevelData::RockEvent& b) { return a.time < b.time; });
    }
    nextRock_ = 0;
    shipX = playerBaseX;
    shipY = playerBaseZ;
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
    baseHits = 0;
    kills = 0;
    gameOver = false;
    cleared = false;
    fireCooldown_ = 0.0f;
    spawnCounter_ = 0;
    fxLaser = 0;
    fxBuild = 0;
}

namespace {
// Cell passability for routing: rocks and built structures
// block; dormant sites and bases do not; a Gateway (kind 25)
// passes only its owner's side.
bool cellBlockedFor(const ArenaSim& sim, int x, int z, bool forPlayer) {
    for (const SimStructure& s : sim.structures) {
        if ((int)s.x != x || (int)s.y != z) {
            continue;
        }
        if (s.rock) {
            return true;
        }
        if (s.state == 0) {
            continue;
        }
        if (s.kind == 25 && ((s.owner == 1) == forPlayer)) {
            continue; // own gateway: pass through
        }
        return true;
    }
    return false;
}
} // namespace

bool ArenaSim::routeExists(int sx, int sz, int tx, int tz, bool forPlayer) const {
    return !findPath(sx, sz, tx, tz, forPlayer).empty() ||
           (sx == tx && sz == tz);
}

std::vector<std::pair<int, int>> ArenaSim::findPath(int sx, int sz, int tx, int tz,
                                                    bool forPlayer) const {
    const int n = (int)extentCells;
    if (sx < 0 || sz < 0 || tx < 0 || tz < 0 || sx >= n || sz >= n || tx >= n ||
        tz >= n) {
        return {};
    }
    std::vector<int> prev((std::size_t)n * n, -2);
    std::queue<std::pair<int, int>> q;
    prev[(std::size_t)sz * n + sx] = -1;
    q.push({sx, sz});
    const int dx[4] = {1, -1, 0, 0};
    const int dz[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        const auto [cx, cz] = q.front();
        q.pop();
        if (cx == tx && cz == tz) {
            break;
        }
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + dx[d], nz = cz + dz[d];
            if (nx < 0 || nz < 0 || nx >= n || nz >= n) {
                continue;
            }
            if (prev[(std::size_t)nz * n + nx] != -2) {
                continue;
            }
            const bool isTarget = (nx == tx && nz == tz);
            if (!isTarget && cellBlockedFor(*this, nx, nz, forPlayer)) {
                continue;
            }
            prev[(std::size_t)nz * n + nx] = cz * n + cx;
            q.push({nx, nz});
        }
    }
    if (prev[(std::size_t)tz * n + tx] == -2) {
        return {};
    }
    std::vector<std::pair<int, int>> path;
    for (int cur = tz * n + tx; cur != -1; cur = prev[(std::size_t)cur]) {
        path.push_back({cur % n, cur / n});
    }
    std::reverse(path.begin(), path.end());
    return path;
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

void ArenaSim::activateNextStructure(int kind, int gx, int gz) {
    // The script names its target: a dormant structure of this
    // kind at the event's cell first, then any dormant of the
    // kind, then a fresh construction at the event's cell.
    SimStructure* target = nullptr;
    for (SimStructure& s : structures) {
        if (!s.rock && s.owner == 0 && s.kind == kind && s.state == 0 &&
            (int)s.x == gx && (int)s.y == gz) {
            target = &s;
            break;
        }
    }
    if (target == nullptr) {
        for (SimStructure& s : structures) {
            if (!s.rock && s.owner == 0 && s.kind == kind && s.state == 0) {
                target = &s;
                break;
            }
        }
    }
    if (target != nullptr) {
        target->state = 1; // construction starts; completes on its timer
        target->timer = kBuildTime;
        return;
    }
    if (gx >= 0 && gz >= 0 && gx < (int)extentCells && gz < (int)extentCells &&
        !cellOccupied(gx, gz)) {
        SimStructure s;
        s.x = (float)gx;
        s.y = (float)gz;
        s.kind = kind;
        s.state = 1;
        s.owner = 0;
        s.hp = structureHp(kind);
        s.timer = kBuildTime;
        s.producedKind = (kind == 28) ? 44 : 40;
        structures.push_back(s);
    }
}

void ArenaSim::destroyShip() {
    ++deaths;
    shipAlive = false;
    respawnTimer = 2.0f; // recovered reset countdown (C0 = 2)
    fxLaser += 2;        // destruction burst (sound/particles in the shell later)
}

void ArenaSim::spawnRock(int amount, float x, float y, float vx) {
    SimEntity e;
    e.kind = 90; // a flying rock: the Thorium source
    e.ground = false;
    e.x = x;
    e.y = y;
    e.vx = vx;
    e.vy = 0.0f;
    e.value = amount;
    e.hp = 1;
    entities.push_back(e);
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
    if ((in.pressed & input::BtnR1) != 0) {
        // Buildable kinds: the weapons, Gateway, and both Ops.
        buildKind = (buildKind == 26) ? 28 : (buildKind >= 28 ? 21 : buildKind + 1);
    }
    // Grab (training string 123: "Grab flying rock: Hold ..."):
    // holding Alt pulls the nearest rock in range onto the ship
    // so it can be dragged to the turrets.
    if (shipAlive && (in.buttons & input::BtnAlt) != 0) {
        SimEntity* best = nullptr;
        float bestD2 = 1.8f * 1.8f;
        for (SimEntity& e : entities) {
            if (!e.alive || e.kind != 90) {
                continue;
            }
            const float dx = shipX - e.x, dy = shipY - e.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) {
                bestD2 = d2;
                best = &e;
            }
        }
        if (best != nullptr) {
            const float dx = shipX - best->x, dy = shipY - best->y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.05f) {
                best->vx = dx / len * 5.0f;
                best->vy = dy / len * 5.0f;
            }
        }
    }
    if (shipAlive && (in.pressed & input::BtnConfirm) != 0 && thorium >= kBuildCost) {
        const int cx = (int)std::floor(shipX);
        const int cy = (int)std::floor(shipY);
        const bool onBase = ((float)cx + 0.5f == playerBaseX && (float)cy + 0.5f == playerBaseZ) ||
                            ((float)cx + 0.5f == enemyBaseX && (float)cy + 0.5f == enemyBaseZ);
        if (!onBase && !cellOccupied(cx, cy)) {
            SimStructure s;
            s.x = (float)cx;
            s.y = (float)cy;
            s.kind = buildKind;
            s.state = 1; // building
            s.owner = 1;
            s.hp = structureHp(buildKind);
            s.timer = kBuildTime;
            s.producedKind = (buildKind == 28) ? 44 : 40;
            structures.push_back(s);
            // "You can't fully block the Enemy Path" (training
            // string 129): the placement is refused if it would
            // seal the route between the bases.
            if (!routeExists((int)enemyBaseX, (int)enemyBaseZ, (int)playerBaseX,
                             (int)playerBaseZ, false)) {
                structures.pop_back();
            } else {
                thorium -= kBuildCost;
                ++fxBuild;
            }
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

    // --- the level's timed script: construction orders by kind,
    // addressed to the event's own cell (record bytes 4/5).
    while (nextEvent < events_.size() && events_[nextEvent].value <= timeSec) {
        activateNextStructure(events_[nextEvent].bytes[3],
                              (int)events_[nextEvent].bytes[0],
                              (int)events_[nextEvent].bytes[1]);
        ++nextEvent;
    }

    // --- the rock schedule (selectors 20/21/22): flying rocks
    // carrying their own Thorium payouts.
    while (nextRock_ < rockEvents_.size() && rockEvents_[nextRock_].time <= timeSec) {
        const LevelData::RockEvent& ev = rockEvents_[nextRock_];
        ++spawnCounter_;
        int amount = ev.amountLo;
        if (ev.amountHi > ev.amountLo) {
            amount += (int)((spawnCounter_ * 7u) %
                            (unsigned)(ev.amountHi - ev.amountLo + 1));
        }
        if (amount <= 0) {
            amount = 20;
        }
        if (ev.hasCell) {
            spawnRock(amount, (float)ev.cellX + 0.5f, (float)ev.cellZ + 0.5f,
                      (spawnCounter_ % 2u == 0u) ? 1.0f : -1.0f);
        } else if (spawnCounter_ % 2u == 0u) {
            spawnRock(amount, -0.5f, 1.0f + (float)(spawnCounter_ % 9u), 1.2f);
        } else {
            spawnRock(amount, extentCells + 0.5f,
                      1.0f + (float)(spawnCounter_ % 9u), -1.2f);
        }
        ++nextRock_;
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
                const float tbx = e.playerOwned ? enemyBaseX : playerBaseX;
                const float tbz = e.playerOwned ? enemyBaseZ : playerBaseZ;
                e.path = findPath((int)e.x, (int)e.y, (int)tbx, (int)tbz,
                                  e.playerOwned);
                e.pathIdx = e.path.empty() ? 0 : 1;
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

    // --- entity advance: rocks drift (or are dragged); ground
    // units follow their BFS route to the opposing base.
    for (SimEntity& e : entities) {
        if (!e.alive) {
            continue;
        }
        if (e.kind == 90) {
            e.x += e.vx * dt;
            e.y += e.vy * dt;
            if (e.x < -2.0f || e.x > extentCells + 2.0f || e.y < -2.0f ||
                e.y > extentCells + 2.0f) {
                e.alive = false;
            }
            continue;
        }
        const float tbx = e.playerOwned ? enemyBaseX : playerBaseX;
        const float tbz = e.playerOwned ? enemyBaseZ : playerBaseZ;
        e.repathTimer -= dt;
        if (e.pathIdx >= e.path.size() || e.repathTimer <= 0.0f) {
            e.repathTimer = 0.75f;
            e.path = findPath((int)std::floor(e.x), (int)std::floor(e.y),
                              (int)tbx, (int)tbz, e.playerOwned);
            e.pathIdx = e.path.empty() ? 0 : 1;
        }
        float mx = 0.0f, my = 0.0f;
        if (e.pathIdx < e.path.size()) {
            const float wx = (float)e.path[e.pathIdx].first + 0.5f;
            const float wy = (float)e.path[e.pathIdx].second + 0.5f;
            const float dx = wx - e.x, dy = wy - e.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < 0.15f) {
                ++e.pathIdx;
            } else {
                mx = dx / d;
                my = dy / d;
            }
        } else {
            // No route (shouldn't happen — construction can't seal
            // the bases off): home straight in.
            const float dx = tbx - e.x, dy = tbz - e.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > 0.01f) {
                mx = dx / d;
                my = dy / d;
            }
        }
        e.x += mx * e.speed * dt;
        e.y += my * e.speed * dt;
        const float bdx = tbx - e.x, bdy = tbz - e.y;
        if (bdx * bdx + bdy * bdy <= 0.7f * 0.7f) {
            e.alive = false;
            if (e.playerOwned) {
                ++score;
                if (score >= winTarget) {
                    cleared = true;
                }
            } else {
                ++baseHits;
                if (baseHits >= baseHitsLimit) {
                    gameOver = true;
                }
            }
            continue;
        }
        // Contact with the ship damages both.
        if (shipAlive && e.alive && !e.playerOwned) {
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
                    if (e.kind == 90 && s.fromPlayer) {
                        thorium = std::min(thoriumMax, thorium + e.value);
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
                    ++kills; // Thorium comes from rocks only (notes 014)
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

    bool anyUnit = false;
    for (const SimEntity& e : entities) {
        anyUnit = anyUnit || e.kind != 90;
    }
    if (nextEvent >= events_.size() && nextRock_ >= rockEvents_.size() &&
        !anyUnit && eventsTotal > 0) {
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
