// arena_sim_test.cpp — drives the arena simulation headlessly
// against the recovered model: primary records are placed content
// at (raw[2], raw[3]); a pre-placed structure settles active iff
// its record level (raw[1]) is at or below its kind's initial
// level; secondary events start construction of dormant
// structures by kind; factories (26/28) stockpile and deploy
// ground units; the player builds funded towers.

#include "../src/arena_sim.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void put32(std::vector<unsigned char>& buf, std::size_t off, std::uint32_t v) {
    buf[off] = (unsigned char)(v >> 24);
    buf[off + 1] = (unsigned char)(v >> 16);
    buf[off + 2] = (unsigned char)(v >> 8);
    buf[off + 3] = (unsigned char)v;
}

// Level fixture: extent 16. Primaries: extent marker; a rock at
// (3,3); a factory (kind 26, record level 1 <= initial 2 -> settles
// active) at (8,2); a tower (kind 21, record level 3 > initial 1 ->
// dormant) at (5,2). Events: t=1 kind 24 (no site -> comet),
// t=2 kind 21 (starts the tower's construction), t=3 kind 26
// (already active -> comet).
comet::LevelData makeLevel() {
    std::vector<unsigned char> buf(136 + 56 * 4 + 24 * 3, 0);
    put32(buf, 0, 4);
    put32(buf, 4, 3);
    unsigned char* rec = buf.data() + 136;
    rec[0] = 0x0B;
    rec[2] = 16;
    rec[3] = 16;
    unsigned char* rock = rec + 56;
    rock[0] = 9;
    rock[2] = 3;
    rock[3] = 3;
    unsigned char* fac = rec + 112;
    fac[0] = 26;
    fac[1] = 1;
    fac[2] = 8;
    fac[3] = 2;
    unsigned char* tow = rec + 168;
    tow[0] = 21;
    tow[1] = 3;
    tow[2] = 5;
    tow[3] = 2;
    unsigned char* sec = rec + 224;
    const float times[3] = {1.0f, 2.0f, 3.0f};
    const unsigned char kinds[3] = {24, 21, 26};
    for (int i = 0; i < 3; ++i) {
        unsigned char* r = sec + i * 24;
        std::uint32_t bits;
        std::memcpy(&bits, &times[i], 4);
        put32(buf, (std::size_t)(r - buf.data()), bits);
        r[6] = 1;
        r[7] = kinds[i];
    }
    comet::LevelData lv;
    if (!lv.loadFromBuffer(buf.data(), buf.size())) {
        std::fprintf(stderr, "FAIL: fixture level parses\n");
        ++failures;
    }
    return lv;
}

const comet::SimStructure* findKind(const comet::ArenaSim& sim, int kind) {
    for (const comet::SimStructure& s : sim.structures) {
        if (s.kind == kind) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    comet::LevelData lv = makeLevel();
    check(lv.events.size() == 3, "fixture keeps 3 events");

    comet::ArenaSim sim;
    sim.reset(&lv);
    check(sim.extentCells == 16.0f, "extent from marker");
    check(sim.structures.size() == 3, "3 placed records (marker skipped)");
    const comet::SimStructure* fac = findKind(sim, 26);
    check(fac != nullptr && fac->x == 8.0f && fac->y == 2.0f,
          "factory placed at (raw[2], raw[3])");
    check(fac != nullptr && fac->state == 2,
          "settlement: record level 1 <= kind 26 initial 2 -> active");
    check(findKind(sim, 21)->state == 0,
          "settlement: record level 3 > kind 21 initial 1 -> dormant");
    check(sim.cellOccupied(3, 3), "rock cell occupied");
    check(!sim.cellOccupied(4, 4), "empty cell free");

    comet::input::InputFrame idle{};
    sim.update(idle, 0.5f);
    sim.update(idle, 0.6f); // t=1.1: kind-24 event, no site -> comet
    bool cometSeen = false;
    for (const comet::SimEntity& e : sim.entities) {
        cometSeen = cometSeen || !e.ground;
    }
    check(cometSeen, "unmatched event sends a comet");

    sim.update(idle, 1.0f); // t=2.1: kind-21 event starts construction
    check(findKind(sim, 21)->state == 1, "event starts tower construction");
    for (int i = 0; i < 30 && findKind(sim, 21)->state != 2; ++i) {
        sim.update(idle, 0.2f);
    }
    check(findKind(sim, 21)->state == 2, "construction completes on its timer");

    // The settled factory stockpiles and deploys a ground unit.
    bool deployed = false;
    for (int i = 0; i < 40 && !deployed; ++i) {
        sim.update(idle, 0.5f);
        for (const comet::SimEntity& e : sim.entities) {
            deployed = deployed || e.ground;
        }
    }
    check(deployed, "factory deploys a ground unit from its stockpile");

    // Ship flight still answers the input vector.
    comet::input::InputFrame left{};
    left.moveX = -1.0f;
    const float x0 = sim.shipX;
    sim.update(left, 0.5f);
    check(sim.shipX < x0 - 3.0f, "ship flies with the input vector");

    // Player construction: funded request, build timer, active tower.
    sim.shipX = 10.5f;
    sim.shipY = 12.5f;
    const int th0 = sim.thorium;
    comet::input::InputFrame build{};
    build.pressed = comet::input::BtnConfirm;
    sim.update(build, 0.05f);
    check(sim.thorium == th0 - 25, "construction debits Thorium");
    const comet::SimStructure* mine = nullptr;
    for (const comet::SimStructure& s : sim.structures) {
        if (s.owner == 1) {
            mine = &s;
        }
    }
    check(mine != nullptr && mine->state == 1, "player tower is building");
    for (int i = 0; i < 60 && mine->state != 2; ++i) {
        sim.update(idle, 0.1f);
    }
    check(mine->state == 2, "player tower completes after its build timer");

    // Occupied cell refuses construction.
    sim.shipX = 3.5f;
    sim.shipY = 3.5f; // the rock
    const int th1 = sim.thorium;
    sim.update(build, 0.05f);
    check(sim.thorium == th1, "no build on an occupied cell");

    if (failures == 0) {
        std::printf("arena_sim_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "arena_sim_test: %d failure(s)\n", failures);
    return 1;
}
