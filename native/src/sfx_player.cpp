// sfx_player.cpp — see sfx_player.h.

#include "sfx_player.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <system_error>

namespace comet {
namespace {

// Filename substrings in the extracted banks, per slot; the
// first matching alternative wins. data1.pak's library uses
// semantic names (turret_shot, money_pickup...); data2.pak's
// are generic (item_choose, LASER...).
const char* kNeedles[static_cast<int>(Sfx::Count)][3] = {
    {"menu_choose", "item_choose", nullptr},          // Choose
    {"build_sound", nullptr, nullptr},                // Build
    {"turret_shot", "LASER", nullptr},                // Laser
    {"EXPLOSION_LARGE", "explode_struct", nullptr},   // Explode
    {"money_pickup", nullptr, nullptr},               // Pickup
    {"range_upgrade", nullptr, nullptr},              // Upgrade
    {"out_of_money", nullptr, nullptr},               // Denied
};

// Soundtrack files are named like "2_02Subversion.mp3".
bool isTrackName(const std::string& n) {
    return n.size() > 4 && n[0] >= '0' && n[0] <= '9' && n[1] == '_' &&
           n[2] >= '0' && n[2] <= '9' && n[3] >= '0' && n[3] <= '9';
}

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
        if (entry.path().extension() != ".mp3" || isTrackName(name)) {
            continue;
        }
        for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
            bool match = false;
            for (int k = 0; k < 3 && kNeedles[i][k] != nullptr; ++k) {
                if (name.find(kNeedles[i][k]) != std::string::npos) {
                    match = true;
                    break;
                }
            }
            if (match && clips_[i].empty()) {
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
    if (stream_ == nullptr) {
        // Fixed output format; each clip's own format is set as
        // the stream's input format in play() and SDL converts.
        // (The data1 bank mixes 44.1k stereo, 48k and 32k clips;
        // the old one-format stream assumed the data2 bank's
        // uniform 44.1k mono.)
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = 44100;
        stream_ = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream_ == nullptr) {
            SDL_Log("sfx: SDL_OpenAudioDeviceStream failed: %s",
                    SDL_GetError());
            return false;
        }
        SDL_ResumeAudioStreamDevice(stream_);
    }
    if (clip.sampleRate != streamRate_ || clip.channels != streamCh_) {
        SDL_AudioSpec in{};
        in.format = SDL_AUDIO_S16;
        in.channels = clip.channels;
        in.freq = clip.sampleRate;
        if (!SDL_SetAudioStreamFormat(stream_, &in, nullptr)) {
            SDL_Log("sfx: SDL_SetAudioStreamFormat failed: %s",
                    SDL_GetError());
            return false;
        }
        streamRate_ = clip.sampleRate;
        streamCh_ = clip.channels;
    }
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

MusicPlayer::~MusicPlayer() { shutdown(); }

void MusicPlayer::shutdown() {
    if (stream_ != nullptr) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    started_ = false;
}

bool MusicPlayer::loadFromDir(const std::string& dir) {
    std::error_code ec;
    std::string fallback;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() != ".mp3" || !isTrackName(name)) {
            continue;
        }
        if (name.rfind("2_02", 0) == 0) {
            fallback = entry.path().string();
            break; // preferred track found
        }
        if (fallback.empty()) {
            fallback = entry.path().string();
        }
    }
    if (fallback.empty()) {
        return false;
    }
    if (!decodeMp3File(fallback.c_str(), clip_)) {
        SDL_Log("music: FAILED to decode %s", fallback.c_str());
        clip_ = SfxClip{};
        return false;
    }
    SDL_Log("music: loaded %s (%zu samples, %d Hz, %d ch)",
            fallback.c_str(), clip_.pcm.size(), clip_.sampleRate,
            clip_.channels);
    return true;
}

void MusicPlayer::start() {
    if (clip_.empty() || started_) {
        return;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = clip_.channels;
    spec.freq = clip_.sampleRate;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                        &spec, nullptr, nullptr);
    if (stream_ == nullptr) {
        SDL_Log("music: SDL_OpenAudioDeviceStream failed: %s",
                SDL_GetError());
        return;
    }
    SDL_SetAudioStreamGain(stream_, 0.4f);
    SDL_ResumeAudioStreamDevice(stream_);
    SDL_PutAudioStreamData(
        stream_, clip_.pcm.data(),
        static_cast<int>(clip_.pcm.size() * sizeof(std::int16_t)));
    started_ = true;
}

void MusicPlayer::tick() {
    if (!started_ || stream_ == nullptr) {
        return;
    }
    // Re-queue the whole track while at least ~2 s remains, so
    // the loop never starves the device.
    const std::int64_t bytesPerSec =
        static_cast<std::int64_t>(clip_.sampleRate) * clip_.channels * 2;
    if (SDL_GetAudioStreamQueued(stream_) < bytesPerSec * 2) {
        SDL_PutAudioStreamData(
            stream_, clip_.pcm.data(),
            static_cast<int>(clip_.pcm.size() * sizeof(std::int16_t)));
    }
}

} // namespace comet
