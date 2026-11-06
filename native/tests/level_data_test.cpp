// level_data_test.cpp — synthetic .map image in the real layout:
// 136-byte header (counts first), then 56-byte orientation records,
// then 24-byte object records. Size validation must reject a buffer
// whose length disagrees with the header counts.

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

void padTo(std::vector<std::uint8_t>& buf, std::size_t n) {
    while (buf.size() < n) {
        buf.push_back(0);
    }
}

} // namespace

int main() {
    std::vector<std::uint8_t> img;
    putBE32(img, 2); // count56
    putBE32(img, 1); // count24
    padTo(img, comet::LevelData::kHeaderBytes);
    // record56 #0: type 9, quat (0, 0.6, 0, 0.8), field 42
    std::size_t recStart = img.size();
    img.push_back(9);
    padTo(img, recStart + 4);
    putBEFloat(img, 0.0f);
    putBEFloat(img, 0.6f);
    putBEFloat(img, 0.0f);
    putBEFloat(img, 0.8f);
    putBE32(img, 42);
    padTo(img, recStart + 56);
    // record56 #1: all zero
    padTo(img, recStart + 112);
    // record24 #0: value 8.5, bytes 5,1,20,21
    putBEFloat(img, 8.5f);
    img.push_back(5);
    img.push_back(1);
    img.push_back(20);
    img.push_back(21);
    padTo(img, recStart + 112 + 24);

    comet::LevelData level;
    check(level.loadFromBuffer(img.data(), img.size()), "parses");
    check(level.count56() == 2 && level.count24() == 1, "counts");
    check(level.records56.size() == 2 && level.records24.size() == 1,
          "record vectors sized");
    if (level.records56.size() == 2) {
        const auto& r = level.records56[0];
        check(r.type == 9, "rec56 type");
        check(r.quat[1] == 0.6f && r.quat[3] == 0.8f, "rec56 quat");
        check(r.field == 42, "rec56 field");
    }
    if (level.records24.size() == 1) {
        const auto& r = level.records24[0];
        check(r.value == 8.5f, "rec24 value");
        check(r.bytes[2] == 20 && r.bytes[3] == 21, "rec24 type/subtype");
    }

    check(!level.loadFromBuffer(img.data(), img.size() - 1),
          "truncated buffer rejected");
    std::vector<std::uint8_t> bad = img;
    bad[3] = 3; // count56 = 3, but only 2 records present
    check(!level.loadFromBuffer(bad.data(), bad.size()),
          "count/size mismatch rejected");

    if (failures == 0) {
        std::printf("level_data_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "level_data_test: %d failure(s)\n", failures);
    return 1;
}
