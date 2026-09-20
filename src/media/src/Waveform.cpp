#include "lumaforge/media/Waveform.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/media/AudioDecoder.h"

#include <algorithm>
#include <cmath>

namespace lf::media {

std::uint8_t Waveform::peakBetween(TimePoint from, TimePoint to) const noexcept {
    if (peaks.empty() || bucket.ticks <= 0 || to.ticks <= from.ticks) return 0;
    const auto n = static_cast<Ticks>(peaks.size());
    Ticks b0 = std::clamp<Ticks>(from.ticks / bucket.ticks, 0, n - 1);
    Ticks b1 = std::clamp<Ticks>((to.ticks - 1) / bucket.ticks, 0, n - 1);
    if (from.ticks >= n * bucket.ticks) return 0;
    std::uint8_t m = 0;
    for (Ticks b = b0; b <= b1; ++b) m = std::max(m, peaks[static_cast<std::size_t>(b)]);
    return m;
}

Result<Waveform> analyseWaveform(const std::string& path) {
    // Peaks do not need full bandwidth: decode mono at 8 kHz (80 samples per
    // 10 ms bucket) - roughly 6x cheaper than the playback path.
    AudioDecoder::Options o;
    o.sampleRate = 8000;
    o.channels = 1;
    auto dec = AudioDecoder::open(path, o);
    if (!dec) return dec.error();
    Waveform w;
    const int perBucket = static_cast<int>(o.sampleRate / 100);
    const Duration total = (*dec)->duration();
    if (total.ticks > 0) w.peaks.reserve(static_cast<std::size_t>(total.ticks / w.bucket.ticks + 1));
    std::vector<float> buf(static_cast<std::size_t>(perBucket) * 64);
    int fill = 0;
    float peak = 0.0f;
    while (true) {
        auto got = (*dec)->read(buf.data(), perBucket * 64);
        if (!got || *got <= 0) break;
        for (int i = 0; i < *got; ++i) {
            peak = std::max(peak, std::fabs(buf[static_cast<std::size_t>(i)]));
            if (++fill == perBucket) {
                w.peaks.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(peak, 0.0f, 1.0f) * 255.0f)));
                fill = 0;
                peak = 0.0f;
            }
        }
        if (w.peaks.size() > 100u * 60u * 60u * 6u) break;   // 6 hours: stop rather than grow forever
    }
    if (fill > 0) w.peaks.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(peak, 0.0f, 1.0f) * 255.0f)));
    LF_LOG_DEBUG("media", "waveform for {}: {} buckets", path, w.peaks.size());
    return w;
}

}  // namespace lf::media
