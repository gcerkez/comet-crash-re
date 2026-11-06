// dds_texture.h — DDS image decoding for the game's textures.
//
// The shipped textures (USRDIR/data/models/**.dds, images/**.dds)
// are standard DDS files: mostly uncompressed 32-bit with channel
// masks, some DXT-compressed. This decoder is mask-driven (any
// channel order works) and handles DXT1/DXT3/DXT5 blocks, level 0
// only. Output is tightly packed R,G,B,A bytes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace comet {

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // width*height*4, R first
    bool empty() const { return width <= 0 || height <= 0; }
};

bool decodeDds(const std::uint8_t* data, std::size_t size, RgbaImage& out);
bool loadDdsFile(const char* path, RgbaImage& out);

} // namespace comet
