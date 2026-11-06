// level_data.h — parser for the game's .map level files.
//
// Format (cracked from FUN_000ee4d8 = level_parse; see
// notes/RECONSTRUCTION-006.md), all big-endian:
//
//   header, 136 bytes:
//     u32[0] = count56, u32[1] = count24, remaining fields undecoded
//   count56 records of 56 bytes (orientation records):
//     u8 type @0, quaternion (4 floats) @4, u32 field @20, rest zero
//   count24 records of 24 bytes (object records):
//     float @0 (7..796 across shipped maps), bytes @4..8 — @6 is a
//     small class (0..2), @7 carries the values level_parse branches
//     on (9, 10, 20, 21-24 seen there), rest zero
//
// File size must be exactly 136 + count56*56 + count24*24 — verified
// against all twelve shipped maps. Levels below 61 are embedded in
// the game's data (the parser reads them from a table); the loose
// .map files are levels 61+ and custom maps.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace comet {

struct LevelData {
    static constexpr std::size_t kHeaderBytes = 136;

    std::array<std::uint32_t, kHeaderBytes / 4> header{};

    struct Record56 {
        std::uint8_t type = 0;
        std::uint8_t b1 = 0, b2 = 0, b3 = 0; // @1..3; for type 0x0B, b2/b3 carry the extent
        float quat[4] = {0, 0, 0, 0};
        std::uint32_t field = 0;
    };
    struct Record24 {
        float value = 0.0f;
        std::uint8_t bytes[20] = {}; // @4..23; selector @6, opcode @7, grid @4/@5
    };

    std::vector<Record56> records56;
    std::vector<Record24> records24;

    // The rock economy (notes/RECONSTRUCTION-014.md): secondary
    // records on selectors 20/21/22 are flying-rock events. A rock
    // carries a Thorium payout in [amountLo, amountHi]; selector 20
    // also names the cell where the rock appears.
    struct RockEvent {
        float time = 0.0f;
        int selector = 0;
        int amountLo = 0;
        int amountHi = 0;
        bool hasCell = false;
        int cellX = 0;
        int cellZ = 0;
    };
    std::vector<RockEvent> rockEvents;

    // Base markers from the primary records: type 10 with
    // raw[1] == 1 is player 1's base; type 29 is the enemy base.
    bool hasPlayerBase = false;
    int playerBaseX = 0, playerBaseZ = 0;
    bool hasEnemyBase = false;
    int enemyBaseX = 0, enemyBaseZ = 0;

    // Derived by level_parse: the type-0x0B record is the arena
    // extent marker — extent = max(b2, b3), defaulting to 24 when
    // no marker exists (the runtime writes it to state +0xA8/+0xAC).
    // `events` is the filtered secondary list: a record is kept
    // when selector (@6) <= 10 and (selector is 9/10 ? opcode (@7)
    // != 29 : opcode in 20..30 and != 29); the section is consumed
    // only when an extent marker exists.
    std::uint32_t extent = 24;
    bool hasExtentMarker = false;
    std::vector<Record24> events;

    std::uint32_t count56() const { return header[0]; }
    std::uint32_t count24() const { return header[1]; }

    // Parses a whole .map file image, validating the exact size the
    // header counts predict. Returns false on any mismatch.
    bool loadFromBuffer(const std::uint8_t* data, std::size_t size);
    bool loadFromFile(const char* path);
};

} // namespace comet
