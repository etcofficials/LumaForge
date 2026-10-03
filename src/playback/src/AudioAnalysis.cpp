#include "lumaforge/playback/AudioAnalysis.h"

#include "lumaforge/media/AudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace lf::playback {

Result<float> measurePeak(const std::string& path, TimePoint start, Duration duration) {
    media::AudioDecoder::Options o;
    o.sampleRate = 48000;
    o.channels = 2;
    auto dec = media::AudioDecoder::open(path, o);
    if (!dec) return dec.error();
    if (auto s = (*dec)->seek(start); !s) return s.error();
    std::int64_t remaining = static_cast<std::int64_t>(duration.toSeconds() * 48000.0 + 0.5);
    std::vector<float> buf(4096 * 2);
    float peak = 0.0f;
    while (remaining > 0) {
        const int want = static_cast<int>(std::min<std::int64_t>(4096, remaining));
        auto got = (*dec)->read(buf.data(), want);
        if (!got) return got.error();
        if (*got == 0) break;
        for (int i = 0; i < *got * 2; ++i) peak = std::max(peak, std::fabs(buf[static_cast<std::size_t>(i)]));
        remaining -= *got;
    }
    return peak;
}

float normalizeGain(float peak, float targetDb) noexcept {
    if (peak <= 1e-6f) return 1.0f;
    return std::pow(10.0f, targetDb / 20.0f) / peak;
}

}  // namespace lf::playback
