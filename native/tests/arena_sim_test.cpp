// arena_sim_test.cpp — drives the arena simulation headlessly:
// scheduled spawns fire on the level's event times, the ship moves
// with the input vector, shots kill entities, leaks cost base hits.

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

comet::LevelData makeLevel() {
    // extent 16 via the 0x0B marker; three spawn events at
    // t=1 (kind 21), t=2 (kind 21), t=3 (kind 26, 2 hp).
    std::vector<unsigned char> buf(136 + 56 + 24 * 3, 0);
    auto put32 = [&buf](std::size_t off, std::uint32_t v) {
        buf[off] = (unsigned char)(v >> 24);
        buf[off + 1] = (unsigned char)(v >> 16);
        buf[off + 2] = (unsigned char)(v >> 8);
        buf[off + 3] = (unsigned char)v;
    };
    put32(0, 1);
    put32(4, 3);
    unsigned char* rec = buf.data() + 136;
    rec[0] = 0x0B;
    rec[2] = 16;
    rec[3] = 16;
    unsigned char* sec = rec + 56;
    const float times[3] = {1.0f, 2.0f, 3.0f};
    const unsigned char kinds[3] = {21, 21, 26};
    for (int i = 0; i < 3; ++i) {
        unsigned char* r = sec + i * 24;
        std::uint32_t bits;
        std::memcpy(&bits, &times[i], 4);
        put32((std::size_t)(r - buf.data()), bits);
        r[6] = 1;           // selector
        r[7] = kinds[i];    // opcode = unit kind
    }
    comet::LevelData lv;
    if (!lv.loadFromBuffer(buf.data(), buf.size())) {
        std::fprintf(stderr, "FAIL: fixture level parses\n");
        ++failures;
    }
    return lv;
}

} // namespace

int main() {
    comet::LevelData lv = makeLevel();
    check(lv.events.size() == 3, "fixture keeps 3 events");

    comet::ArenaSim sim;
    sim.reset(&lv);
    check(sim.extentCells == 16.0f, "extent from marker");
    check(sim.eventsTotal == 3, "3 events scheduled");

    comet::input::InputFrame idle{};
    sim.update(idle, 0.5f);
    check(sim.entities.empty(), "nothing spawns before t=1");
    sim.update(idle, 0.6f); // t=1.1
    check(sim.entities.size() == 1, "first event spawned");
    check(sim.entities[0].kind == 21, "kind from opcode");

    // Fly left for a second: the ship must move.
    comet::input::InputFrame left{};
    left.moveX = -1.0f;
    const float x0 = sim.shipX;
    sim.update(left, 0.5f);
    check(sim.shipX < x0 - 3.0f, "ship flies with the input vector");

    // Let the remaining events spawn, then let everything leak:
    // base takes 3 hits and the sim is not over (limit 10).
    for (int i = 0; i < 40 && !sim.entities.empty(); ++i) {
        sim.update(idle, 0.5f);
    }
    check(sim.nextEvent == 3u, "all events consumed");
    check(sim.baseHits == 3, "leaked entities hit the base");
    check(!sim.gameOver, "3 hits is not game over");
    check(sim.cleared, "wave cleared once the field is empty");

    // Combat: fresh sim, spawn one event, park the ship under the
    // entity's column and fire upward until it dies.
    sim.reset(&lv);
    sim.update(idle, 1.1f); // spawn #1
    check(sim.entities.size() == 1, "combat: one entity");
    sim.shipX = sim.entities[0].x;
    sim.shipY = sim.entities[0].y + 6.0f;
    comet::input::InputFrame fire{};
    fire.buttons = comet::input::BtnAction;
    bool killed = false;
    for (int i = 0; i < 60 && !killed; ++i) {
        sim.update(fire, 0.05f);
        killed = sim.kills > 0;
    }
    check(killed, "shots kill the entity");
    check(sim.fxLaser > 0, "laser fx counted");

    if (failures == 0) {
        std::printf("arena_sim_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "arena_sim_test: %d failure(s)\n", failures);
    return 1;
}
