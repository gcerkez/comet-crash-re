// arena_sim.h — the playable arena simulation (native milestone 8).
//
// Built from the recovered structure of the original (see
// notes/RECONSTRUCTION-011.md for the cross-resource audit):
//   * the arena is an extent x extent grid of cells (the level's
//     type-0x0B extent marker, default 24);
//   * PRIMARY records are pre-placed content at grid cell
//     (raw[2], raw[3]) with the record's quaternion as orientation:
//     type 9 = rock, types 20..28 = structures (dormant until the
//     level's script activates them), 0x0B = the extent marker;
//   * SECONDARY records are the level's timed script: when an
//     event's time key passes, it activates a dormant structure of
//     the matching kind (opcode). Factories then produce ground
//     units; towers fire on their own;
//   * the player flies the ship, builds towers with Thorium
//     (construction is a funded request with a build timer, per
//     the recovered construction flow), and defends the base edge.
//
// v1 simplifications are listed in RECONSTRUCTION-011.md.

#pragma once

#include <cstddef>
#include <vector>

#include "input_types.h"
#include "level_data.h"

namespace comet {

struct SimEntity { // a moving unit (ground unit or comet)
    float x = 0, y = 0;
    int kind = 0;
    float speed = 0;
    int hp = 1;
    bool alive = true;
    bool ground = true;
};

struct SimShot {
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    bool alive = true;
    bool fromPlayer = true;
};

struct SimStructure {
    float x = 0, y = 0;   // cell coordinates (record raw[2], raw[3])
    int kind = 0;         // record type / build kind (20..28)
    int state = 0;        // 0 dormant, 1 building, 2 active
    int owner = 0;        // 0 = the level (enemy), 1 = the player
    int hp = 3;
    float timer = 0.0f;   // build / production clock
    bool rock = false;
};

class ArenaSim {
public:
    void reset(const LevelData* level);
    void update(const input::InputFrame& in, float dt);

    // True when cell (x, y) holds a rock or an existing structure.
    bool cellOccupied(int x, int y) const;

    float extentCells = 24.0f;
    float shipX = 12.0f, shipY = 22.0f;
    float shipVX = 0.0f, shipVY = 0.0f;
    float shipYaw = 0.0f;

    float timeSec = 0.0f;
    float timeScale = 1.0f;
    std::size_t nextEvent = 0;
    int eventsTotal = 0;

    std::vector<SimEntity> entities;
    std::vector<SimShot> shots;
    std::vector<SimStructure> structures; // incl. rocks (kind 9)

    int thorium = 100;
    int buildKind = 21; // currently selected build (cycle with Alt)
    int baseHits = 0;
    int baseHitsLimit = 10;
    int kills = 0;
    bool gameOver = false;
    bool cleared = false;

    int fxLaser = 0;
    int fxBuild = 0;

private:
    std::vector<LevelData::Record24> events_;
    float fireCooldown_ = 0.0f;
    unsigned spawnCounter_ = 0;
    void activateNextStructure(int kind);
    void fireShot(float x, float y, float dx, float dy, bool fromPlayer);
};

} // namespace comet
