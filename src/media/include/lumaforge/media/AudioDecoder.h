#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/media/MediaInfo.h"

#include <memory>
#include <string>

namespace lf::media {

// Decodes one file's audio stream into interleaved 32-bit float at a fixed
// output rate/channel count (the mixer's format), with sample-accurate
// seeking. Used from one thread at a time.
class AudioDecoder {
public:
    struct Options {
        int sampleRate = 48000;
        int channels = 2;
    };

    static Result<std::unique_ptr<AudioDecoder>> open(const std::string& utf8Path, const Options& options);
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    // Next read() returns the sample at exactly `t` (media time).
    Status seek(TimePoint t);

    // Fills up to `frames` interleaved frames; returns the number written
    // (fewer only at end of stream, 0 == EOF). Silence is never fabricated.
    Result<int> read(float* out, int frames);

    [[nodiscard]] TimePoint position() const noexcept;   // media time of the next sample
    [[nodiscard]] Duration duration() const noexcept { return m_duration; }
    [[nodiscard]] const AudioStreamInfo& info() const noexcept { return m_info; }
    [[nodiscard]] int sampleRate() const noexcept { return m_rate; }
    [[nodiscard]] int channels() const noexcept { return m_channels; }

private:
    AudioDecoder() = default;
    Status decodeMore();   // pushes more resampled samples into the FIFO; sets EOF

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    AudioStreamInfo m_info;
    Duration m_duration;
    int m_rate = 48000;
    int m_channels = 2;
    std::int64_t m_positionSamples = 0;   // in output samples, media time origin
    std::int64_t m_skipSamples = 0;       // samples to discard after a seek
    bool m_eof = false;
};

}  // namespace lf::media
