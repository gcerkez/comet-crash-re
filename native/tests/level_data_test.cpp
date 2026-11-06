// level_data_test.cpp — synthetic .map image: header + float pairs,
// of which only the unit-magnitude direction pairs must be kept.

#include "../src/level_data.h"

#include <cstdint>
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

void putBE32(std::vector<std::uint8_t>& buf, std::uint32_t v) {
    buf.push_back(std::uint8_t(v >> 24));
    buf.push_back(std::uint8_t(v >> 16));
    buf.push_back(std::uint8_t(v >> 8));
    buf.push_back(std::uint8_t(v));
}

void putBEFloat(std::vector<std::uint8_t>& buf, float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    putBE32(buf, bits);
}

} // namespace

int main() {
    std::vector<std::uint8_t> img;
    for (std::uint32_t i = 0; i < 8; ++i) {
        putBE32(img, 100 + i);
    }
    putBEFloat(img, 0.6f);
    putBEFloat(img, 0.8f); // keep: unit vector
    putBEFloat(img, 0.0f);
    putBEFloat(img, 0.0f); // drop: zero
    putBEFloat(img, 500.0f);
    putBEFloat(img, 10.0f); // drop: position-like, not a direction
    putBEFloat(img, -1.0f);
    putBEFloat(img, 0.0f); // keep: axis direction

    comet::LevelData level;
    check(level.loadFromBuffer(img.data(), img.size()), "parses");
    check(level.header[0] == 100 && level.header[7] == 107, "header fields");
    check(level.vectors.size() == 2, "two directions kept");
    if (level.vectors.size() == 2) {
        check(level.vectors[0][0] == 0.6f && level.vectors[0][1] == 0.8f,
              "first vector");
        check(level.vectors[1][0] == -1.0f && level.vectors[1][1] == 0.0f,
              "second vector");
    }

    comet::LevelData empty;
    check(!empty.loadFromBuffer(img.data(), 16), "short buffer rejected");

    if (failures == 0) {
        std::printf("level_data_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "level_data_test: %d failure(s)\n", failures);
    return 1;
}
