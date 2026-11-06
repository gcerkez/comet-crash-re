// level_data.h — exploratory parser for the game's .map level files.
//
// Verified format facts (2026-11-05, notes/RECONSTRUCTION-004.md):
// a header of eight big-endian u32 fields, then a body of big-endian
// float data. Large sections of the body are unit-magnitude 2D
// direction vectors (components in [-1, 1], |v| ~= 1 — flow-field-like
// data, mostly zero where a cell carries no direction); other values
// run to ~800, so position-like sections exist too. The exact section
// layout is still being pinned down from the loader code; this parser
// extracts the direction vectors so the native host can render real
// level data today. Header fields are exposed raw.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace comet {

struct LevelData {
    std::array<std::uint32_t, 8> header{};
    // Direction vectors in file order (unit-magnitude float pairs).
    std::vector<std::array<float, 2>> vectors;

    // Parses a whole .map file image. Returns false when the buffer is
    // too small to hold the header.
    bool loadFromBuffer(const std::uint8_t* data, std::size_t size);
    bool loadFromFile(const char* path);
};

} // namespace comet
