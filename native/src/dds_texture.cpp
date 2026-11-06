// dds_texture.cpp — see dds_texture.h.

#include "dds_texture.h"

#include <cstdio>
#include <cstring>

namespace comet {
namespace {

std::uint32_t rd32(const std::uint8_t* p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

int maskShift(std::uint32_t mask) {
    int s = 0;
    while (mask != 0 && (mask & 1u) == 0) {
        mask >>= 1;
        ++s;
    }
    return s;
}

std::uint8_t channel(const std::uint8_t* px, std::uint32_t mask, int bytesPerPx) {
    if (mask == 0) {
        return 0;
    }
    std::uint32_t v = 0;
    for (int i = bytesPerPx - 1; i >= 0; --i) {
        v = (v << 8) | px[i];
    }
    const int shift = maskShift(mask);
    const std::uint32_t field = (v & mask) >> shift;
    const std::uint32_t maxField = mask >> shift;
    if (maxField == 0) {
        return 0;
    }
    return (std::uint8_t)((field * 255u) / maxField);
}

void putPx(RgbaImage& img, int x, int y, int r, int g, int b, int a) {
    std::uint8_t* d = &img.rgba[((std::size_t)y * img.width + x) * 4];
    d[0] = (std::uint8_t)r;
    d[1] = (std::uint8_t)g;
    d[2] = (std::uint8_t)b;
    d[3] = (std::uint8_t)a;
}

void expand565(std::uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31;
    g = ((c >> 5) & 63) * 255 / 63;
    b = (c & 31) * 255 / 31;
}

// Decodes one DXT color block (DXT1 semantics; `fourColor` forces
// the 4-color palette even when c0 <= c1, as DXT3/DXT5 require).
void decodeColorBlock(const std::uint8_t* blk, RgbaImage& img, int bx, int by,
                      bool fourColor, int alphaOverride[16]) {
    const std::uint16_t c0 = (std::uint16_t)(blk[0] | (blk[1] << 8));
    const std::uint16_t c1 = (std::uint16_t)(blk[2] | (blk[3] << 8));
    int pal[4][3];
    expand565(c0, pal[0][0], pal[0][1], pal[0][2]);
    expand565(c1, pal[1][0], pal[1][1], pal[1][2]);
    if (c0 > c1 || fourColor) {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = (2 * pal[0][i] + pal[1][i]) / 3;
            pal[3][i] = (pal[0][i] + 2 * pal[1][i]) / 3;
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = (pal[0][i] + pal[1][i]) / 2;
        }
        pal[3][0] = pal[3][1] = pal[3][2] = 0;
    }
    const std::uint32_t bits = rd32(blk + 4);
    for (int i = 0; i < 16; ++i) {
        const int x = bx + (i % 4), y = by + (i / 4);
        if (x >= img.width || y >= img.height) {
            continue;
        }
        const int idx = (int)((bits >> (i * 2)) & 3u);
        int a = 255;
        if (alphaOverride != nullptr) {
            a = alphaOverride[i];
        } else if (c0 <= c1 && !fourColor && idx == 3) {
            a = 0; // DXT1 transparent texel
        }
        putPx(img, x, y, pal[idx][0], pal[idx][1], pal[idx][2], a);
    }
}

bool decodeDxt(const std::uint8_t* data, std::size_t size, std::size_t offset,
               int kind, RgbaImage& out) {
    const int blocksX = (out.width + 3) / 4;
    const int blocksY = (out.height + 3) / 4;
    const std::size_t blockBytes = (kind == 1) ? 8 : 16;
    if (offset + (std::size_t)blocksX * blocksY * blockBytes > size) {
        return false;
    }
    const std::uint8_t* p = data + offset;
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const std::uint8_t* blk = p + ((std::size_t)by * blocksX + bx) * blockBytes;
            int alpha[16];
            int* alphaPtr = nullptr;
            if (kind == 3) {
                for (int i = 0; i < 16; ++i) {
                    const std::uint8_t nib = (i % 2 == 0) ? (blk[i / 2] & 0x0F)
                                                          : (blk[i / 2] >> 4);
                    alpha[i] = nib * 17;
                }
                alphaPtr = alpha;
                decodeColorBlock(blk + 8, out, bx * 4, by * 4, true, alphaPtr);
            } else if (kind == 5) {
                const int a0 = blk[0], a1 = blk[1];
                int apal[8];
                apal[0] = a0;
                apal[1] = a1;
                if (a0 > a1) {
                    for (int i = 2; i < 8; ++i) {
                        apal[i] = ((8 - i) * a0 + (i - 1) * a1) / 7;
                    }
                } else {
                    for (int i = 2; i < 6; ++i) {
                        apal[i] = ((6 - i) * a0 + (i - 1) * a1) / 5;
                    }
                    apal[6] = 0;
                    apal[7] = 255;
                }
                std::uint64_t abits = 0;
                for (int i = 0; i < 6; ++i) {
                    abits |= (std::uint64_t)blk[2 + i] << (i * 8);
                }
                for (int i = 0; i < 16; ++i) {
                    alpha[i] = apal[(abits >> (i * 3)) & 7u];
                }
                alphaPtr = alpha;
                decodeColorBlock(blk + 8, out, bx * 4, by * 4, true, alphaPtr);
            } else {
                decodeColorBlock(blk, out, bx * 4, by * 4, false, nullptr);
            }
        }
    }
    return true;
}

} // namespace

bool decodeDds(const std::uint8_t* data, std::size_t size, RgbaImage& out) {
    if (data == nullptr || size < 128 || std::memcmp(data, "DDS ", 4) != 0) {
        return false;
    }
    const std::uint8_t* hdr = data + 4;
    if (rd32(hdr) != 124) {
        return false;
    }
    out.height = (int)rd32(hdr + 8);
    out.width = (int)rd32(hdr + 12);
    if (out.width <= 0 || out.height <= 0 || out.width > 8192 || out.height > 8192) {
        return false;
    }
    const std::uint8_t* pf = hdr + 72; // DDS_PIXELFORMAT at file offset 76
    const std::uint32_t fourCC = rd32(pf + 8);
    out.rgba.assign((std::size_t)out.width * out.height * 4, 0);

    if (fourCC != 0) {
        int kind = 0;
        if (std::memcmp(&fourCC, "DXT1", 4) == 0) {
            kind = 1;
        } else if (std::memcmp(&fourCC, "DXT3", 4) == 0) {
            kind = 3;
        } else if (std::memcmp(&fourCC, "DXT5", 4) == 0) {
            kind = 5;
        } else {
            return false;
        }
        return decodeDxt(data, size, 128, kind, out);
    }

    const std::uint32_t bitCount = rd32(pf + 12);
    if (bitCount != 16 && bitCount != 24 && bitCount != 32) {
        return false;
    }
    const int bpp = (int)bitCount / 8;
    const std::uint32_t rMask = rd32(pf + 16);
    const std::uint32_t gMask = rd32(pf + 20);
    const std::uint32_t bMask = rd32(pf + 24);
    const std::uint32_t aMask = rd32(pf + 28);
    const std::size_t need = 128 + (std::size_t)out.width * out.height * bpp;
    if (size < need) {
        return false;
    }
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const std::uint8_t* px = data + 128 + ((std::size_t)y * out.width + x) * bpp;
            const int a = aMask != 0 ? channel(px, aMask, bpp) : 255;
            putPx(out, x, y, channel(px, rMask, bpp), channel(px, gMask, bpp),
                  channel(px, bMask, bpp), a);
        }
    }
    return true;
}

bool loadDdsFile(const char* path, RgbaImage& out) {
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
    std::vector<std::uint8_t> buf((std::size_t)len);
    const std::size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return got == buf.size() && decodeDds(buf.data(), buf.size(), out);
}

} // namespace comet
