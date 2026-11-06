// level_data.cpp — see level_data.h for the format state.

#include "level_data.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace comet {
namespace {

std::uint32_t readBE32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

float readBEFloat(const std::uint8_t* p) {
    const std::uint32_t bits = readBE32(p);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// A direction vector: both components finite and in [-1, 1], with a
// magnitude near 1 (the stored vectors are normalized; a loose band
// tolerates rounding and the occasional scaled entry).
bool isDirection(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    if (x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f) {
        return false;
    }
    const float mag = std::sqrt(x * x + y * y);
    return mag >= 0.5f && mag <= 1.5f;
}

} // namespace

bool LevelData::loadFromBuffer(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < header.size() * 4) {
        return false;
    }
    for (std::size_t i = 0; i < header.size(); ++i) {
        header[i] = readBE32(data + i * 4);
    }
    vectors.clear();
    for (std::size_t off = header.size() * 4; off + 8 <= size; off += 8) {
        const float x = readBEFloat(data + off);
        const float y = readBEFloat(data + off + 4);
        if (isDirection(x, y)) {
            vectors.push_back({x, y});
        }
    }
    return true;
}

bool LevelData::loadFromFile(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        std::fclose(f);
        return false;
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(len));
    const std::size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return got == buf.size() && loadFromBuffer(buf.data(), buf.size());
}

} // namespace comet
