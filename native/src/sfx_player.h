// sfx_player.h — plays the game's extracted MP3 sound effects.
//
// The original routes SFX through Sony's MultiStream engine over
// cellAudio. The native stand-in is deliberately small: MP3s are
// decoded once at load (minimp3, vendored under third_party/) and the
// PCM is queued onto an SDL audio stream when a sound is triggered.
// The extracted bank is uniformly 44.1 kHz mono, which is the stream
// format; loading a clip in another format fails loudly in the log
// rather than resampling silently.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace comet {

enum class Sfx { Choose = 0, Build, Laser, Count };

struct SfxClip {
    std::vector<std::int16_t> pcm;
    int sampleRate = 0;
    int channels = 0;
    bool empty() const { return pcm.empty(); }
};

// Decodes an MP3 file into a clip (implemented in mp3_decode.cpp).
bool decodeMp3File(const char* path, SfxClip& out);

class SfxPlayer {
public:
    SfxPlayer() = default;
    ~SfxPlayer();
    SfxPlayer(const SfxPlayer&) = delete;
    SfxPlayer& operator=(const SfxPlayer&) = delete;

    // Scans a directory of extracted .mp3 files and loads the clips
    // whose names match the known slots (substring match, so the
    // bank's numeric prefixes don't matter). Returns clips loaded.
    int loadFromDir(const std::string& dir);

    bool has(Sfx s) const { return !clips_[static_cast<int>(s)].empty(); }
    std::size_t clipSamples(Sfx s) const {
        return clips_[static_cast<int>(s)].pcm.size();
    }

    // Queues the clip for immediate playback (fire-and-forget, like a
    // one-shot MultiStream voice). No-op until a clip is loaded.
    void play(Sfx s);

    // Destroys the audio stream. Must run before SDL_Quit(); the
    // destructor calls it too, but a player that outlives SDL_Quit in
    // scope order would otherwise destroy the stream too late.
    void shutdown();

private:
    bool ensureStream(const SfxClip& clip);

    SfxClip clips_[static_cast<int>(Sfx::Count)];
    SDL_AudioStream* stream_ = nullptr;
};

} // namespace comet
