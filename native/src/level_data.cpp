// level_data.cpp — see level_data.h for the format.

#include "level_data.h"

#include <cstdio>
#include <cstring>
#include <vector>

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

} // namespace

bool LevelData::loadFromBuffer(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < kHeaderBytes) {
        return false;
    }
    for (std::size_t i = 0; i < header.size(); ++i) {
        header[i] = readBE32(data + i * 4);
    }
    const std::size_t c56 = header[0];
    const std::size_t c24 = header[1];
    if (size != kHeaderBytes + c56 * 56 + c24 * 24) {
        return false;
    }
    records56.clear();
    records24.clear();
    const std::uint8_t* p = data + kHeaderBytes;
    for (std::size_t i = 0; i < c56; ++i, p += 56) {
        Record56 r;
        r.type = p[0];
        r.b1 = p[1];
        r.b2 = p[2];
        r.b3 = p[3];
        for (int k = 0; k < 4; ++k) {
            r.quat[k] = readBEFloat(p + 4 + k * 4);
        }
        r.field = readBE32(p + 20);
        records56.push_back(r);
    }
    for (std::size_t i = 0; i < c24; ++i, p += 24) {
        Record24 r;
        r.value = readBEFloat(p);
        for (int k = 0; k < 20; ++k) {
            r.bytes[k] = p[4 + k];
        }
        records24.push_back(r);
    }
    // Rock events and base markers.
    rockEvents.clear();
    hasPlayerBase = hasEnemyBase = false;
    for (const Record24& r : records24) {
        const int selector = (int)r.bytes[2];
        if (selector < 20 || selector > 22) {
            continue;
        }
        RockEvent ev;
        ev.time = r.value;
        ev.selector = selector;
        if (selector == 20) {
            ev.amountLo = (int)r.bytes[6];
            ev.amountHi = (int)r.bytes[7];
            ev.cellX = (int)r.bytes[0];
            ev.cellZ = (int)r.bytes[1];
            ev.hasCell = ev.cellX != 0 || ev.cellZ != 0;
        } else {
            ev.amountLo = ev.amountHi = (int)r.bytes[3];
        }
        if (ev.amountHi < ev.amountLo) {
            ev.amountHi = ev.amountLo;
        }
        rockEvents.push_back(ev);
    }
    for (const Record56& r : records56) {
        if (r.type == 10 && r.b1 == 1) {
            hasPlayerBase = true;
            playerBaseX = (int)r.b2;
            playerBaseZ = (int)r.b3;
        } else if (r.type == 29) {
            hasEnemyBase = true;
            enemyBaseX = (int)r.b2;
            enemyBaseZ = (int)r.b3;
        }
    }

    // Derive the arena extent from the type-0x0B marker record and
    // filter the secondary records into the event list, mirroring
    // level_parse (the secondary section is consumed only when a
    // marker exists).
    extent = 24;
    hasExtentMarker = false;
    for (const Record56& r : records56) {
        if (r.type == 0x0B) {
            hasExtentMarker = true;
            extent = r.b2 > r.b3 ? r.b2 : r.b3;
            break;
        }
    }
    events.clear();
    if (hasExtentMarker) {
        for (const Record24& r : records24) {
            const std::uint8_t selector = r.bytes[2];
            const std::uint8_t opcode = r.bytes[3];
            if (selector > 10) {
                continue;
            }
            const bool keep = (selector == 9 || selector == 10)
                                  ? (opcode != 29)
                                  : (opcode >= 20 && opcode <= 30 && opcode != 29);
            if (keep) {
                events.push_back(r);
            }
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
