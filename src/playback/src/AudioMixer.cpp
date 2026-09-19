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

AudioMixer::AudioMixer(int sampleRate, int channels) : m_rate(sampleRate), m_channels(channels) {}

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

void AudioMixer::render(float* out, int frames) {
    std::memset(out, 0, static_cast<std::size_t>(frames) * m_channels * sizeof(float));
    if (!m_playing.load()) return;

    std::unique_lock lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;   // never stall the device thread; one silent block is inaudible
    if (!m_program) return;

    const TimePoint now{ticksFromTimeBase(m_timeSamples, Rational{1, m_rate})};
    m_requests.clear();
    m_program->audioClipsAt(now, m_requests);
    const float master = m_muted.load() ? 0.0f : m_masterVolume.load();
    m_scratch.resize(static_cast<std::size_t>(frames) * m_channels);

    for (const AudioClipRequest& req : m_requests) {
        ClipReader* reader = readerFor(req);
        if (!reader || reader->failed || !reader->decoder) continue;
        auto& dec = *reader->decoder;
        // Keep the reader aligned with where the program says this clip should be.
        const Ticks drift = dec.position().ticks - req.sourceTime.ticks;
        if (drift < -kResyncTolerance || drift > kResyncTolerance) {
            if (auto s = dec.seek(req.sourceTime); !s) continue;
        }
        auto got = dec.read(m_scratch.data(), frames);
        if (!got) continue;
        const int n = *got;
        const float gain = req.gain * master;
        if (gain == 0.0f) continue;
        for (int i = 0; i < n * m_channels; ++i) out[i] += m_scratch[static_cast<std::size_t>(i)] * gain;
    }
    // Soft clip to avoid harsh wraparound when clips stack.
    for (int i = 0; i < frames * m_channels; ++i) {
        float v = out[i];
        if (v > 1.0f) v = 1.0f;
        else if (v < -1.0f) v = -1.0f;
        out[i] = v;
    }
    m_timeSamples += frames;
    if (m_generation % 64 == 0) pruneReaders();
}

}  // namespace lf::playback
