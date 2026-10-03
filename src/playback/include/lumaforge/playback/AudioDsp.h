#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Small, allocation-free audio processors used by the mixer (preview and
// export share them). Interleaved stereo float, one instance per clip/bus.
namespace lf::playback::dsp {

[[nodiscard]] inline float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
[[nodiscard]] inline float gainToDb(float g) noexcept { return g > 1e-9f ? 20.0f * std::log10(g) : -180.0f; }

// RBJ cookbook biquad, transposed direct form II, per channel state.
class Biquad {
public:
    enum class Type { LowShelf, Peak, HighShelf };
    void set(Type type, double sampleRate, double freq, double gainDb, double q = 0.707);
    [[nodiscard]] bool isFlat() const noexcept { return m_flat; }
    void process(float* interleaved, int frames, int channels) noexcept;
    void reset() noexcept { m_z1 = {}; m_z2 = {}; }

private:
    double m_b0 = 1, m_b1 = 0, m_b2 = 0, m_a1 = 0, m_a2 = 0;
    std::array<double, 8> m_z1{}, m_z2{};
    bool m_flat = true;
};

// Three-band EQ: low shelf 120 Hz, peak 1 kHz, high shelf 8 kHz.
class Equalizer {
public:
    void set(double sampleRate, float lowDb, float midDb, float highDb);
    void process(float* interleaved, int frames, int channels) noexcept;
    void reset() noexcept { m_low.reset(); m_mid.reset(); m_high.reset(); }

private:
    Biquad m_low, m_mid, m_high;
    float m_lowDb = 1e9f, m_midDb = 1e9f, m_highDb = 1e9f;   // force the first set()
    double m_rate = 0;
};

// Feed-forward compressor with a soft knee; stereo-linked peak envelope.
class Compressor {
public:
    void set(double sampleRate, float thresholdDb, float ratio, float attackMs = 10.0f, float releaseMs = 120.0f);
    void process(float* interleaved, int frames, int channels) noexcept;
    void reset() noexcept { m_env = 0.0f; }
    [[nodiscard]] float lastGainReductionDb() const noexcept { return m_lastGrDb; }

private:
    float m_threshold = -18.0f, m_ratio = 3.0f, m_attack = 0.0f, m_release = 0.0f;
    float m_env = 0.0f;
    float m_lastGrDb = 0.0f;
    float m_makeup = 1.0f;
};

// Downward gate: below the threshold the signal fades out (5 ms attack, 80 ms release).
class Gate {
public:
    void set(double sampleRate, float thresholdDb);
    void process(float* interleaved, int frames, int channels) noexcept;
    void reset() noexcept { m_env = 0.0f; m_gain = 0.0f; }

private:
    float m_threshold = 0.0f, m_open = 0.0f, m_close = 0.0f, m_envRelease = 0.0f;
    float m_env = 0.0f, m_gain = 0.0f;
};

// Brick-wall-ish limiter for the master bus: instant attack, 50 ms release,
// ceiling -0.3 dBFS; never lets a sample exceed the ceiling.
class Limiter {
public:
    void set(double sampleRate, float ceilingDb = -0.3f);
    void process(float* interleaved, int frames, int channels) noexcept;

private:
    float m_ceiling = 0.966f, m_release = 0.0f, m_gain = 1.0f;
};

// Equal-power stereo pan: -1 = left, 0 = centre (unity), 1 = right.
inline void pan(float* interleaved, int frames, float position) noexcept {
    if (position == 0.0f) return;
    const float p = std::clamp(position, -1.0f, 1.0f);
    const float angle = (p + 1.0f) * 0.25f * 3.14159265f;
    const float l = std::cos(angle) * 1.41421356f, r = std::sin(angle) * 1.41421356f;
    for (int i = 0; i < frames; ++i) {
        interleaved[2 * i] *= l;
        interleaved[2 * i + 1] *= r;
    }
}

}  // namespace lf::playback::dsp
