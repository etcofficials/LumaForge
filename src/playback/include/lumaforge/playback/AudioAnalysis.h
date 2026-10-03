#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"

#include <string>

namespace lf::playback {

// Highest absolute sample value (linear, 0..1) of a file's audio over a media
// range, decoded at 48 kHz stereo. Blocking: call from a worker thread.
Result<float> measurePeak(const std::string& utf8Path, TimePoint start, Duration duration);

// Gain that brings `peak` to `targetDb` dBFS (e.g. -1), or 1 when silent.
[[nodiscard]] float normalizeGain(float peak, float targetDb) noexcept;

}  // namespace lf::playback
