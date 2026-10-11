#pragma once

#include "lumaforge/core/Time.h"
#include "lumaforge/media/AudioDecoder.h"
#include "lumaforge/playback/AudioDsp.h"
#include "lumaforge/playback/Program.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace lf::playback {

// Pulls audio for the current program time from per-clip decoders and sums
// it into the output buffer. Called from the audio device thread; program
// and transport changes arrive from other threads through the mutex-guarded
// setters. Decoding happens inline: AAC/PCM decode of a 40 ms block is far
// below the callback budget.
class AudioMixer {
public:
    AudioMixer(int sampleRate, int channels);

    void setProgram(std::shared_ptr<const Program> program);
    // Aligns the mixer's clock to program time t (play start, seek).
    void setTime(TimePoint t);
    void setPlaying(bool playing);
    void setMasterVolume(float linear) { m_masterVolume = linear; }
    void setMuted(bool muted) { m_muted = muted; }

    [[nodiscard]] TimePoint time() const;
    [[nodiscard]] bool isPlaying() const noexcept { return m_playing.load(); }

    // Audio device callback: fills `frames` interleaved frames.
    void render(float* out, int frames);

    // Peak levels (linear, 0..1+) since the last call, per audio track and for
    // the master bus; resets the peaks. Safe from any thread.
    struct Levels {
        std::vector<std::pair<std::uint64_t, float>> tracks;   // trackKey -> peak
        float master = 0.0f;
    };
    Levels takeLevels();

private:
    struct ClipReader {
        std::unique_ptr<media::AudioDecoder> decoder;
        std::uint64_t lastUsedGeneration = 0;
        bool failed = false;
        dsp::Equalizer eq;
        dsp::Compressor compressor;
        dsp::Gate gate;
    };
    ClipReader* readerFor(const AudioClipRequest& req);
    void mixClip(ClipReader& reader, const AudioClipRequest& req, int frames);
    void pruneReaders();

    int m_rate;
    int m_channels;
    std::mutex m_mutex;                       // guards program/time/readers
    std::shared_ptr<const Program> m_program;
    std::int64_t m_timeSamples = 0;           // program time in output samples
    std::atomic<bool> m_playing{false};
    std::atomic<float> m_masterVolume{1.0f};
    std::atomic<bool> m_muted{false};
    std::unordered_map<std::uint64_t, ClipReader> m_readers;   // by clipKey
    std::uint64_t m_generation = 0;
    std::vector<float> m_scratch;
    std::vector<float> m_speedScratch;
    std::vector<AudioClipRequest> m_requests;
    std::unordered_map<std::uint64_t, std::vector<float>> m_trackMix;   // per-track bus for the current block
    dsp::Limiter m_limiter;
    float m_limiterCeilingDb = -0.3f;
    std::mutex m_levelMutex;
    std::unordered_map<std::uint64_t, float> m_trackPeaks;
    float m_masterPeak = 0.0f;
};

}  // namespace lf::playback
