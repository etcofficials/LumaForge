#include "lumaforge/playback/AudioMixer.h"

#include "lumaforge/core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lf::playback {

namespace {
constexpr const char* kLog = "audio";
// Re-seek a clip reader when its position drifts more than this from the expected source time.
constexpr Ticks kResyncTolerance = kFlicksPerSecond / 50;   // 20 ms
}  // namespace

AudioMixer::AudioMixer(int sampleRate, int channels) : m_rate(sampleRate), m_channels(channels) { m_limiter.set(sampleRate); }

AudioMixer::Levels AudioMixer::takeLevels() {
    std::lock_guard lock(m_levelMutex);
    Levels l;
    for (const auto& [key, peak] : m_trackPeaks) l.tracks.emplace_back(key, peak);
    l.master = m_masterPeak;
    for (auto& [key, peak] : m_trackPeaks) peak = 0.0f;
    m_masterPeak = 0.0f;
    return l;
}

void AudioMixer::setProgram(std::shared_ptr<const Program> program) {
    std::lock_guard lock(m_mutex);
    m_program = std::move(program);
    ++m_generation;
    pruneReaders();
}

void AudioMixer::setTime(TimePoint t) {
    std::lock_guard lock(m_mutex);
    m_timeSamples = timeBaseFromTicks(std::max<Ticks>(0, t.ticks), Rational{1, m_rate});
}

void AudioMixer::setPlaying(bool playing) { m_playing = playing; }

TimePoint AudioMixer::time() const {
    std::lock_guard lock(const_cast<std::mutex&>(m_mutex));
    return TimePoint{ticksFromTimeBase(m_timeSamples, Rational{1, m_rate})};
}

AudioMixer::ClipReader* AudioMixer::readerFor(const AudioClipRequest& req) {
    auto it = m_readers.find(req.clipKey);
    if (it == m_readers.end()) {
        ClipReader r;
        media::AudioDecoder::Options o;
        o.sampleRate = m_rate;
        o.channels = m_channels;
        auto dec = media::AudioDecoder::open(req.path, o);
        if (!dec) {
            LF_LOG_WARN(kLog, "no audio reader for {}: {}", req.path, dec.error().fullText());
            r.failed = true;
        } else {
            r.decoder = std::move(dec.value());
        }
        it = m_readers.emplace(req.clipKey, std::move(r)).first;
    }
    it->second.lastUsedGeneration = m_generation;
    return &it->second;
}

void AudioMixer::pruneReaders() {
    // Readers unused for a few generations (program changes) are dropped.
    for (auto it = m_readers.begin(); it != m_readers.end();) {
        if (m_generation - it->second.lastUsedGeneration > 4) it = m_readers.erase(it);
        else ++it;
    }
}

void AudioMixer::mixClip(ClipReader& reader, const AudioClipRequest& req, int frames) {
    // Clip processing, gain and pan on m_scratch, then into the clip's track bus.
    float* x = m_scratch.data();
    if (!req.fx.isIdentity()) {
        reader.eq.set(m_rate, req.fx.eqLow, req.fx.eqMid, req.fx.eqHigh);
        reader.eq.process(x, frames, m_channels);
        if (req.fx.gate) {
            reader.gate.set(m_rate, req.fx.gateThreshold);
            reader.gate.process(x, frames, m_channels);
        }
        if (req.fx.compressor) {
            reader.compressor.set(m_rate, req.fx.compThreshold, req.fx.compRatio);
            reader.compressor.process(x, frames, m_channels);
        }
    }
    if (req.gain == 0.0f) return;
    auto& bus = m_trackMix[req.trackKey];
    bus.resize(static_cast<std::size_t>(frames) * m_channels, 0.0f);
    if (m_channels == 2 && req.pan != 0.0f) dsp::pan(x, frames, req.pan);
    for (int i = 0; i < frames * m_channels; ++i) bus[static_cast<std::size_t>(i)] += x[i] * req.gain;
}

void AudioMixer::render(float* out, int frames) {
    std::memset(out, 0, static_cast<std::size_t>(frames) * m_channels * sizeof(float));
    if (!m_playing.load()) return;

    std::unique_lock lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;   // never stall the device thread; one silent block is inaudible
    if (!m_program) return;

    const TimePoint now{ticksFromTimeBase(m_timeSamples, Rational{1, m_rate})};
    m_requests.clear();
    m_program->audioClipsAt(now, m_requests);
    // Preview-only output volume / mute (the export mixer leaves these at unity).
    const float output = m_muted.load() ? 0.0f : m_masterVolume.load();
    m_scratch.resize(static_cast<std::size_t>(frames) * m_channels);
    for (auto& [key, bus] : m_trackMix) std::fill(bus.begin(), bus.end(), 0.0f);

    for (const AudioClipRequest& req : m_requests) {
        ClipReader* reader = readerFor(req);
        if (!reader || reader->failed || !reader->decoder) continue;
        auto& dec = *reader->decoder;
        if (req.reverse) {
            // Backwards: read the block that ends at sourceTime forwards, then flip it.
            const int want = std::clamp(static_cast<int>(std::lround(frames * req.speed)), 1, frames * 16);
            const Ticks span = ticksFromTimeBase(want, Rational{1, m_rate});
            if (auto s = dec.seek(TimePoint{std::max<Ticks>(0, req.sourceTime.ticks - span)}); !s) continue;
            m_speedScratch.resize(static_cast<std::size_t>(want + 1) * m_channels);
            auto got = dec.read(m_speedScratch.data(), want);
            if (!got || *got <= 1) continue;
            const int src = *got;
            for (int i = 0; i < frames; ++i) {
                const double pos = static_cast<double>(frames - 1 - i) * (src - 1) / std::max(1, frames - 1);
                const int i0 = static_cast<int>(pos);
                const int i1 = std::min(i0 + 1, src - 1);
                const float f = static_cast<float>(pos - i0);
                for (int ch = 0; ch < m_channels; ++ch) {
                    const float a = m_speedScratch[static_cast<std::size_t>(i0 * m_channels + ch)];
                    const float b = m_speedScratch[static_cast<std::size_t>(i1 * m_channels + ch)];
                    m_scratch[static_cast<std::size_t>(i * m_channels + ch)] = a + (b - a) * f;
                }
            }
            mixClip(*reader, req, frames);
            continue;
        }
        // Keep the reader aligned with where the program says this clip should be.
        const Ticks drift = dec.position().ticks - req.sourceTime.ticks;
        if (drift < -kResyncTolerance || drift > kResyncTolerance) {
            if (auto s = dec.seek(req.sourceTime); !s) continue;
        }
        int n = 0;
        if (req.speed > 0.0 && std::fabs(req.speed - 1.0) > 1e-6) {
            // Variable speed: read speed * frames source frames and resample
            // linearly (pitch follows speed, like tape). Bounded so a 10x clip
            // cannot ask for an unbounded block.
            const int want = std::clamp(static_cast<int>(std::lround(frames * req.speed)), 1, frames * 16);
            m_speedScratch.resize(static_cast<std::size_t>(want + 1) * m_channels);
            auto got = dec.read(m_speedScratch.data(), want);
            if (!got || *got <= 1) continue;
            const int src = *got;
            for (int i = 0; i < frames; ++i) {
                const double pos = static_cast<double>(i) * (src - 1) / std::max(1, frames - 1);
                const int i0 = static_cast<int>(pos);
                const int i1 = std::min(i0 + 1, src - 1);
                const float f = static_cast<float>(pos - i0);
                for (int ch = 0; ch < m_channels; ++ch) {
                    const float a = m_speedScratch[static_cast<std::size_t>(i0 * m_channels + ch)];
                    const float b = m_speedScratch[static_cast<std::size_t>(i1 * m_channels + ch)];
                    m_scratch[static_cast<std::size_t>(i * m_channels + ch)] = a + (b - a) * f;
                }
            }
            n = frames;
        } else {
            auto got = dec.read(m_scratch.data(), frames);
            if (!got) continue;
            n = *got;
        }
        if (n < frames) std::fill(m_scratch.begin() + static_cast<std::ptrdiff_t>(n) * m_channels, m_scratch.end(), 0.0f);
        mixClip(*reader, req, frames);
    }

    // Track buses: volume, pan, meters; then the master stage.
    std::unordered_map<std::uint64_t, float> peaks;
    for (const AudioClipRequest& req : m_requests) {
        auto it = m_trackMix.find(req.trackKey);
        if (it == m_trackMix.end() || peaks.contains(req.trackKey)) continue;
        auto& bus = it->second;
        for (float& v : bus) v *= req.trackGain;
        if (m_channels == 2) dsp::pan(bus.data(), frames, req.trackPan);
        float peak = 0.0f;
        for (int i = 0; i < frames * m_channels; ++i) { out[i] += bus[static_cast<std::size_t>(i)]; peak = std::max(peak, std::fabs(bus[static_cast<std::size_t>(i)])); }
        peaks[req.trackKey] = peak;
    }
    const float master = m_program->masterGain();
    for (int i = 0; i < frames * m_channels; ++i) out[i] *= master;
    if (m_program->masterLimiter()) m_limiter.process(out, frames, m_channels);
    float masterPeak = 0.0f;
    for (int i = 0; i < frames * m_channels; ++i) {
        // Never wrap around: clamp what the limiter (if off) let through.
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
        masterPeak = std::max(masterPeak, std::fabs(out[i]));
        out[i] *= output;
    }
    {
        std::lock_guard levels(m_levelMutex);
        for (const auto& [key, peak] : peaks) m_trackPeaks[key] = std::max(m_trackPeaks[key], peak);
        m_masterPeak = std::max(m_masterPeak, masterPeak);
    }
    m_timeSamples += frames;
    if (m_generation % 64 == 0) pruneReaders();
}

}  // namespace lf::playback
