#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lf::media {

// Peak envelope of a file's audio: one 0..255 peak per bucket of fixed
// duration, mono (max over channels). Small enough to keep for every media
// item (10 ms buckets = 100 bytes per second of audio).
struct Waveform {
    Duration bucket{kFlicksPerSecond / 100};   // 10 ms
    std::vector<std::uint8_t> peaks;

    [[nodiscard]] bool empty() const noexcept { return peaks.empty(); }
    [[nodiscard]] Duration duration() const noexcept { return Duration{bucket.ticks * static_cast<Ticks>(peaks.size())}; }
    // Max peak over the media time range [from, to) (0 when out of range).
    [[nodiscard]] std::uint8_t peakBetween(TimePoint from, TimePoint to) const noexcept;
};

// Decodes the whole audio stream (mono, reduced rate) and folds it into peaks.
// Blocking: call from a worker thread.
Result<Waveform> analyseWaveform(const std::string& utf8Path);

}  // namespace lf::media
