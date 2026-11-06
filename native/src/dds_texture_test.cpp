// dds_texture_test.cpp — synthetic DDS buffers decoded and checked:
// an uncompressed A8R8G8B8 image and a DXT1 block.

#include "../src/dds_texture.h"

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

void put32(std::vector<std::uint8_t>& b, std::size_t off, std::uint32_t v) {
    b[off] = (std::uint8_t)v;
    b[off + 1] = (std::uint8_t)(v >> 8);
    b[off + 2] = (std::uint8_t)(v >> 16);
    b[off + 3] = (std::uint8_t)(v >> 24);
}

std::vector<std::uint8_t> makeHeader(int w, int h) {
    std::vector<std::uint8_t> b(128, 0);
    std::memcpy(b.data(), "DDS ", 4);
    put32(b, 4, 124);
    put32(b, 12, (std::uint32_t)h);
    put32(b, 16, (std::uint32_t)w);
    put32(b, 76, 32); // pixel format size
    return b;
}

} // namespace

int main() {
    // Uncompressed 2x2, A8R8G8B8 masks.
    {
        std::vector<std::uint8_t> b = makeHeader(2, 2);
        // DDS_PIXELFORMAT starts at file offset 76: size, flags,
        // fourCC, bit count, then the R/G/B/A masks.
        put32(b, 76 + 0, 32);          // pf size
        put32(b, 76 + 8, 0);           // fourCC: none (uncompressed)
        put32(b, 76 + 12, 32);         // RGB bit count
        put32(b, 76 + 16, 0x00FF0000); // R mask
        put32(b, 76 + 20, 0x0000FF00); // G mask
        put32(b, 76 + 24, 0x000000FF); // B mask
        put32(b, 76 + 28, 0xFF000000); // A mask
        b.resize(128 + 16);
        // pixels (BGRA byte order in file for A8R8G8B8): red, green / blue, white
        const std::uint8_t px[16] = {0, 0, 255, 255, 0, 255, 0, 255,
                                     255, 0, 0, 255, 255, 255, 255, 255};
        std::memcpy(b.data() + 128, px, 16);
        comet::RgbaImage img;
        check(comet::decodeDds(b.data(), b.size(), img), "uncompressed DDS decodes");
        check(img.width == 2 && img.height == 2, "dimensions");
        check(img.rgba[0] == 255 && img.rgba[1] == 0 && img.rgba[2] == 0 &&
                  img.rgba[3] == 255,
              "first pixel is red");
        check(img.rgba[4] == 0 && img.rgba[5] == 255 && img.rgba[6] == 0,
              "second pixel is green");
        check(img.rgba[8] == 0 && img.rgba[9] == 0 && img.rgba[10] == 255,
              "third pixel is blue");
    }
    // 16-bit uncompressed, font-page layout: R/G/B masks all on
    // the low byte (luminance), alpha on the high byte.
    {
        std::vector<std::uint8_t> b = makeHeader(2, 1);
        put32(b, 76 + 0, 32);
        put32(b, 76 + 8, 0);
        put32(b, 76 + 12, 16);
        put32(b, 76 + 16, 0x000000FF);
        put32(b, 76 + 20, 0x000000FF);
        put32(b, 76 + 24, 0x000000FF);
        put32(b, 76 + 28, 0x0000FF00);
        b.resize(128 + 4);
        const std::uint8_t px[4] = {200, 128, 60, 255}; // (L,A) x2
        std::memcpy(b.data() + 128, px, 4);
        comet::RgbaImage img;
        check(comet::decodeDds(b.data(), b.size(), img), "16-bit DDS decodes");
        check(img.rgba[0] == 200 && img.rgba[1] == 200 && img.rgba[2] == 200 &&
                  img.rgba[3] == 128,
              "16-bit pixel 0 is luminance + alpha");
        check(img.rgba[4] == 60 && img.rgba[7] == 255, "16-bit pixel 1");
    }
    // DXT1: one 4x4 block, endpoints red/green, all indices 0 -> all red.
    {
        std::vector<std::uint8_t> b = makeHeader(4, 4);
        std::memcpy(b.data() + 76 + 8, "DXT1", 4);
        b.resize(128 + 8);
        b[128] = 0x00;
        b[129] = 0xF8; // c0 = 0xF800 (red)
        b[130] = 0xE0;
        b[131] = 0x07; // c1 = 0x07E0 (green)
        // indices all zero (bytes 132..135 already 0)
        comet::RgbaImage img;
        check(comet::decodeDds(b.data(), b.size(), img), "DXT1 decodes");
        check(img.rgba[0] == 255 && img.rgba[1] == 0 && img.rgba[2] == 0 &&
                  img.rgba[3] == 255,
              "DXT1 texel is red");
    }
    if (failures == 0) {
        std::printf("dds_texture_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "dds_texture_test: %d failure(s)\n", failures);
    return 1;
}
