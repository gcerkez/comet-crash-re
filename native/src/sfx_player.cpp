// sfx_player.cpp — see sfx_player.h.

#include "sfx_player.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <system_error>

namespace comet {
namespace {

// Filename substrings in the extracted data2.pak bank, per slot.
const char* kNeedles[static_cast<int>(Sfx::Count)] = {
    "item_choose", // menu choose / confirm click
    "build_sound", // build placement sound
    "LASER",       // laser fire
};

} // namespace

SfxPlayer::~SfxPlayer() { shutdown(); }

void SfxPlayer::shutdown() {
    if (stream_ != nullptr) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

int SfxPlayer::loadFromDir(const std::string& dir) {
    int loaded = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() != ".mp3") {
            continue;
        }
        for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
            if (name.find(kNeedles[i]) != std::string::npos &&
                clips_[i].empty()) {
                SfxClip clip;
                if (decodeMp3File(entry.path().string().c_str(), clip)) {
                    SDL_Log("sfx: loaded %s (%zu samples, %d Hz, %d ch)",
                            name.c_str(), clip.pcm.size(), clip.sampleRate,
                            clip.channels);
                    clips_[i] = std::move(clip);
                    ++loaded;
                } else {
                    SDL_Log("sfx: FAILED to decode %s", name.c_str());
                }
            }
        }
    }
    return loaded;
}

bool SfxPlayer::ensureStream(const SfxClip& clip) {
    if (stream_ != nullptr) {
        return true;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = clip.channels;
    spec.freq = clip.sampleRate;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                        &spec, nullptr, nullptr);
    if (stream_ == nullptr) {
        SDL_Log("sfx: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void SfxPlayer::play(Sfx s) {
    const SfxClip& clip = clips_[static_cast<int>(s)];
    if (clip.empty() || !ensureStream(clip)) {
        return;
    }
    SDL_PutAudioStreamData(stream_, clip.pcm.data(),
                           static_cast<int>(clip.pcm.size() * sizeof(std::int16_t)));
}

} // namespace comet
