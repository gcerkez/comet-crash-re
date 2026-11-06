// mp3_decode.cpp — the one translation unit that instantiates
// minimp3 (CC0, vendored in third_party/minimp3) for the host.

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimp3_ex.h"

#include "sfx_player.h"

#include <cstdio>

namespace comet {

bool decodeMp3File(const char* path, SfxClip& out) {
    mp3dec_t dec;
    mp3dec_file_info_t info;
    if (mp3dec_load(&dec, path, &info, nullptr, nullptr) != 0 ||
        info.samples == 0 || info.buffer == nullptr) {
        return false;
    }
    out.pcm.assign(info.buffer, info.buffer + info.samples);
    out.sampleRate = info.hz;
    out.channels = info.channels;
    std::free(info.buffer);
    return true;
}

} // namespace comet
