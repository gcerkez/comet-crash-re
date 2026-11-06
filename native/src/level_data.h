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
//     float @0, bytes @4..7 (incl. type @6, subtype @7), rest zero
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
        float quat[4] = {0, 0, 0, 0};
        std::uint32_t field = 0;
    };
    struct Record24 {
        float value = 0.0f;
        std::uint8_t bytes[4] = {0, 0, 0, 0}; // @4..7; type @6, subtype @7
    };

    std::vector<Record56> records56;
    std::vector<Record24> records24;

    std::uint32_t count56() const { return header[0]; }
    std::uint32_t count24() const { return header[1]; }

    // Parses a whole .map file image, validating the exact size the
    // header counts predict. Returns false on any mismatch.
    bool loadFromBuffer(const std::uint8_t* data, std::size_t size);
    bool loadFromFile(const char* path);
};

} // namespace comet
