// arena_sim.h — the playable arena simulation (native milestone 7).
//
// A first faithful-structure gameplay core, built from the pieces
// the reconstruction has established:
//   * the arena is an extent x extent grid of cells (the level's
//     type-0x0B extent marker, default 24);
//   * the level's filtered secondary records are scheduled spawn
//     events: (time key, selector, opcode), opcode = unit kind;
//   * the player ship flies on the grid from the same InputFrame
//     the original's frame_input consumed.
// Spawn positions, speeds, and the event time scale are v1 choices
// (documented in the .cpp) — the record *consumers* in the
// original are still being recovered, and this core is where their
// recovered behavior will land.

#pragma once

#include <cstddef>
#include <vector>

#include "input_types.h"
#include "level_data.h"

namespace comet {

struct SimEntity {
    float x = 0, y = 0;   // cell coordinates
    int kind = 0;         // unit kind (event opcode, 20..30)
    float speed = 0;      // cells per second, toward the base edge
    int hp = 1;
    bool alive = true;
};

struct SimShot {
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    bool alive = true;
};

class ArenaSim {
public:
    // Loads a level (extent + spawn events). Falls back to a bare
    // 24x24 arena with no events when level is null.
    void reset(const LevelData* level);

    // Advances one frame. dt is seconds (already scaled by the
    // shell's time factor for event pacing).
    void update(const input::InputFrame& in, float dt);

    float extentCells = 24.0f;
    float shipX = 12.0f, shipY = 22.0f;
    float shipVX = 0.0f, shipVY = 0.0f;
    float shipYaw = 0.0f; // radians, for rendering

    float timeSec = 0.0f;
    float timeScale = 1.0f; // schedule clock rate vs real time (v1: uncalibrated)
    std::size_t nextEvent = 0;
    int eventsTotal = 0;

    std::vector<SimEntity> entities;
    std::vector<SimShot> shots;

    int baseHits = 0;
    int baseHitsLimit = 10;
    int kills = 0;
    bool gameOver = false;
    bool cleared = false;

    // One-shot effect counters the shell drains each frame to drive
    // the SFX bank (laser on fire/kill).
    int fxLaser = 0;

private:
    std::vector<LevelData::Record24> events_;
    float fireCooldown_ = 0.0f;
    unsigned spawnCounter_ = 0;
};

} // namespace comet
