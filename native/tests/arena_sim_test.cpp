// arena_sim_test.cpp — drives the arena simulation headlessly
// against the recovered model: primary records are placed content
// at (raw[2], raw[3]); secondary events activate dormant
// structures by kind; factories produce ground units; the player
// builds funded towers that complete on a timer and fire.

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

// Level fixture: extent 16. Primaries: extent marker, a rock at
// (3,3), a dormant factory (kind 24) at (8,2), a dormant tower
// (kind 21) at (5,2). Events: t=1 activate 24, t=2 activate 21,
// t=3 kind 26 (no dormant match -> comet fallback).
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
    fac[0] = 24;
    fac[2] = 8;
    fac[3] = 2;
    unsigned char* tow = rec + 168;
    tow[0] = 21;
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
    const comet::SimStructure* fac = findKind(sim, 24);
    check(fac != nullptr && fac->x == 8.0f && fac->y == 2.0f,
          "factory placed at (raw[2], raw[3])");
    check(fac != nullptr && fac->state == 0, "factory starts dormant");
    check(sim.cellOccupied(3, 3), "rock cell occupied");
    check(!sim.cellOccupied(4, 4), "empty cell free");

    comet::input::InputFrame idle{};
    sim.update(idle, 0.5f);
    check(findKind(sim, 24)->state == 0, "nothing activates before t=1");
    sim.update(idle, 0.6f); // t=1.1
    check(findKind(sim, 24)->state == 2, "event activates the factory");
    check(findKind(sim, 21)->state == 0, "tower still dormant");

    // The factory produces a ground unit on its interval.
    bool produced = false;
    for (int i = 0; i < 30 && !produced; ++i) {
        sim.update(idle, 0.5f);
        produced = !sim.entities.empty();
    }
    check(produced, "active factory produces a ground unit");
    check(sim.entities[0].ground, "produced unit is a ground unit");

    // Ship flight still answers the input vector.
    comet::input::InputFrame left{};
    left.moveX = -1.0f;
    const float x0 = sim.shipX;
    sim.update(left, 0.5f);
    check(sim.shipX < x0 - 3.0f, "ship flies with the input vector");

    // Construction: funded request at the ship's cell, build timer,
    // then an active player tower.
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
    check(mine != nullptr && mine->state == 1, "tower is building");
    for (int i = 0; i < 60 && mine->state != 2; ++i) {
        sim.update(idle, 0.1f);
    }
    check(mine->state == 2, "tower completes after its build timer");

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
