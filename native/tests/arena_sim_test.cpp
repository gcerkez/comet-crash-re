// arena_sim_test.cpp — drives the arena simulation headlessly
// against the recovered model: primary records are placed content
// at (raw[2], raw[3]); a pre-placed structure settles active iff
// its record level (raw[1]) is at or below its kind's initial
// level; secondary events start construction of dormant
// structures by kind; factories (26/28) stockpile and deploy
// ground units; the player builds funded towers.

#include "../src/arena_sim.h"

#include <cmath>
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
    std::vector<unsigned char> buf(136 + 56 * 6 + 24 * 5, 0);
    put32(buf, 0, 6);
    put32(buf, 4, 5);
    put32(buf, 16, 300); // header[4]: starting Thorium (hypothesis)
    put32(buf, 20, 1);   // header[5]: units-to-win (hypothesis)
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
    unsigned char* pbase = rec + 224;
    pbase[0] = 10;
    pbase[1] = 1; // player 1
    pbase[2] = 8;
    pbase[3] = 14;
    unsigned char* ebase = rec + 280;
    ebase[0] = 29;
    ebase[1] = 1;
    ebase[2] = 8;
    ebase[3] = 1;
    unsigned char* sec = rec + 336;
    const float times[3] = {1.0f, 2.0f, 3.0f};
    const unsigned char kinds[3] = {24, 21, 26};
    const unsigned char cells[3][2] = {{12, 12}, {5, 2}, {8, 2}};
    for (int i = 0; i < 3; ++i) {
        unsigned char* r = sec + i * 24;
        std::uint32_t bits;
        std::memcpy(&bits, &times[i], 4);
        put32(buf, (std::size_t)(r - buf.data()), bits);
        r[4] = cells[i][0];
        r[5] = cells[i][1];
        r[6] = 1;
        r[7] = kinds[i];
    }
    unsigned char* rk1 = sec + 72;
    {
        const float t1 = 1.5f;
        std::uint32_t bits;
        std::memcpy(&bits, &t1, 4);
        put32(buf, (std::size_t)(rk1 - buf.data()), bits);
        rk1[6] = 21; // rock channel, payout in the opcode
        rk1[7] = 40;
    }
    unsigned char* rk2 = sec + 96;
    {
        const float t2 = 2.5f;
        std::uint32_t bits;
        std::memcpy(&bits, &t2, 4);
        put32(buf, (std::size_t)(rk2 - buf.data()), bits);
        rk2[4] = 6; // appears at cell (6, 6)
        rk2[5] = 6;
        rk2[6] = 20; // rock channel with a payout range
        rk2[10] = 35;
        rk2[11] = 35;
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
    check(lv.events.size() == 3, "fixture keeps 3 construction events");
    check(lv.rockEvents.size() == 2, "fixture keeps 2 rock events");
    check(lv.rockEvents[0].amountLo == 40, "sel-21 rock pays its opcode");
    check(lv.rockEvents[1].hasCell && lv.rockEvents[1].amountLo == 35,
          "sel-20 rock carries cell + payout");
    check(lv.hasPlayerBase && lv.playerBaseX == 8 && lv.playerBaseZ == 14,
          "player base marker decoded");
    check(lv.hasEnemyBase && lv.enemyBaseX == 8 && lv.enemyBaseZ == 1,
          "enemy base marker decoded");

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
    check(sim.thorium == 300, "starting Thorium from header[4]");
    check(sim.winTarget == 1, "win target from header[5]");
    check(sim.cellOccupied(3, 3), "rock cell occupied");
    check(!sim.cellOccupied(4, 4), "empty cell free");

    comet::input::InputFrame idle{};
    sim.update(idle, 0.5f);
    sim.update(idle, 0.6f); // t=1.1: kind-24 order at cell (12,12)
    const comet::SimStructure* built = findKind(sim, 24);
    check(built != nullptr && built->state == 1 && built->x == 12.0f &&
              built->y == 12.0f,
          "script creates construction at the event's own cell");

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

    // Routed: deployed units march on the player base cell and
    // score a base hit when they arrive.
    {
        bool arrived = false;
        for (int i = 0; i < 240 && !arrived; ++i) {
            sim.update(idle, 0.25f);
            arrived = sim.baseHits >= 1;
        }
        check(arrived, "routed unit reaches the player base cell");
    }

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

    // Ship damage, destruction, and the 2-second reset.
    {
        comet::ArenaSim s2;
        s2.reset(&lv);
        // Activate the tower quickly via its script event, park the
        // ship in range, and let it shoot.
        s2.update(idle, 2.1f); // t=2.1: tower construction starts
        for (int i = 0; i < 20 && findKind(s2, 21)->state != 2; ++i) {
            s2.update(idle, 0.25f);
        }
        s2.shipX = 5.5f;
        s2.shipY = 4.5f;
        bool hurt = false;
        for (int i = 0; i < 600 && !hurt; ++i) {
            s2.update(idle, 0.02f); // fine steps: shots move 0.48/step
            hurt = s2.shipHealth < 100;
        }
        check(hurt, "enemy tower fire damages the ship");
        s2.shipHealth = 5;
        bool died = false;
        for (int i = 0; i < 600 && !died; ++i) {
            s2.update(idle, 0.02f);
            died = !s2.shipAlive;
        }
        check(died && s2.deaths == 1, "ship is destroyed at zero health");
        for (int i = 0; i < 20 && !s2.shipAlive; ++i) {
            s2.update(idle, 0.25f);
        }
        check(s2.shipAlive && s2.shipHealth == 100,
              "ship resets at the base after the countdown");
    }

    // Offense: a player Basic Ops sends a unit to the enemy base
    // and wins the stage (winTarget = 1 in this fixture).
    {
        comet::ArenaSim s3;
        s3.reset(&lv);
        s3.buildKind = 26;
        s3.shipX = 10.5f;
        s3.shipY = 12.5f;
        comet::input::InputFrame b{};
        b.pressed = comet::input::BtnConfirm;
        s3.update(b, 0.05f);
        bool won = false;
        for (int i = 0; i < 400 && !won; ++i) {
            s3.update(idle, 0.25f);
            won = s3.cleared;
        }
        check(won && s3.score >= 1, "player unit reaches the enemy base and wins");
    }

    // Thorium: shooting down a flying rock pays the rock's own
    // amount (40 for the sel-21 rock, 35 for the sel-20 rock).
    {
        comet::ArenaSim s4;
        s4.reset(&lv);
        const int thStart = s4.thorium;
        int gained = 0;
        for (int i = 0; i < 900 && gained == 0; ++i) {
            for (const comet::SimEntity& e : s4.entities) {
                if (e.kind == 90) {
                    s4.shipX = e.x;
                    s4.shipY = e.y;
                }
            }
            comet::input::InputFrame f{};
            f.buttons = comet::input::BtnAction;
            f.aimY = -1.0f;
            s4.update(f, 0.02f);
            gained = s4.thorium - thStart;
        }
        check(gained == 40 || gained == 35 || gained == 75,
              "downed rock pays its own Thorium amount");
    }

    // Grab: holding Alt drags a nearby rock toward the ship.
    {
        comet::ArenaSim s5;
        s5.reset(&lv);
        s5.update(idle, 2.6f); // both rocks have appeared
        const comet::SimEntity* rock = nullptr;
        for (const comet::SimEntity& e : s5.entities) {
            if (e.kind == 90 && e.value == 35) {
                rock = &e;
            }
        }
        check(rock != nullptr, "sel-20 rock appeared at its cell");
        if (rock != nullptr) {
            s5.shipX = rock->x + 1.2f;
            s5.shipY = rock->y;
            const float d0 = 1.2f;
            comet::input::InputFrame grab{};
            grab.buttons = comet::input::BtnAlt;
            for (int i = 0; i < 25; ++i) {
                s5.update(grab, 0.02f);
            }
            float d1 = 99.0f;
            for (const comet::SimEntity& e : s5.entities) {
                if (e.kind == 90 && e.value == 35) {
                    const float dx = s5.shipX - e.x, dy = s5.shipY - e.y;
                    d1 = std::sqrt(dx * dx + dy * dy);
                }
            }
            check(d1 < d0 - 0.3f, "held grab pulls the rock toward the ship");
        }
    }

    if (failures == 0) {
        std::printf("arena_sim_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "arena_sim_test: %d failure(s)\n", failures);
    return 1;
}
