#include "lumaforge/playback/AudioDsp.h"

#include <algorithm>

namespace lf::playback::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
float coefficient(double sampleRate, float ms) { return ms <= 0.0f ? 0.0f : static_cast<float>(std::exp(-1.0 / (sampleRate * ms / 1000.0))); }
}  // namespace

// ---------------------------------------------------------------- biquad

void Biquad::set(Type type, double sampleRate, double freq, double gainDb, double q) {
    m_flat = std::fabs(gainDb) < 0.01;
    if (m_flat) { m_b0 = 1; m_b1 = m_b2 = m_a1 = m_a2 = 0; return; }
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / sampleRate;
    const double cs = std::cos(w0), sn = std::sin(w0);
    const double alpha = sn / (2.0 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type) {
        case Type::Peak:
            b0 = 1 + alpha * A; b1 = -2 * cs; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cs; a2 = 1 - alpha / A;
            break;
        case Type::LowShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cs + s); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - s);
            a0 = (A + 1) + (A - 1) * cs + s; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - s;
            break;
        }
        case Type::HighShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cs + s); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - s);
            a0 = (A + 1) - (A - 1) * cs + s; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - s;
            break;
        }
    }
    m_b0 = b0 / a0; m_b1 = b1 / a0; m_b2 = b2 / a0; m_a1 = a1 / a0; m_a2 = a2 / a0;
}

void Biquad::process(float* x, int frames, int channels) noexcept {
    if (m_flat) return;
    const int ch = std::min(channels, 8);
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < ch; ++c) {
            const double in = x[i * channels + c];
            const double out = m_b0 * in + m_z1[c];
            m_z1[c] = m_b1 * in - m_a1 * out + m_z2[c];
            m_z2[c] = m_b2 * in - m_a2 * out;
            x[i * channels + c] = static_cast<float>(out);
        }
    }
}

// ---------------------------------------------------------------- EQ

void Equalizer::set(double sampleRate, float lowDb, float midDb, float highDb) {
    if (sampleRate == m_rate && lowDb == m_lowDb && midDb == m_midDb && highDb == m_highDb) return;
    m_rate = sampleRate;
    m_lowDb = lowDb; m_midDb = midDb; m_highDb = highDb;
    m_low.set(Biquad::Type::LowShelf, sampleRate, 120.0, lowDb);
    m_mid.set(Biquad::Type::Peak, sampleRate, 1000.0, midDb, 0.9);
    m_high.set(Biquad::Type::HighShelf, sampleRate, 8000.0, highDb);
}

void Equalizer::process(float* x, int frames, int channels) noexcept {
    m_low.process(x, frames, channels);
    m_mid.process(x, frames, channels);
    m_high.process(x, frames, channels);
}

// ---------------------------------------------------------------- compressor

void Compressor::set(double sampleRate, float thresholdDb, float ratio, float attackMs, float releaseMs) {
    m_threshold = thresholdDb;
    m_ratio = std::max(1.0f, ratio);
    m_attack = coefficient(sampleRate, attackMs);
    m_release = coefficient(sampleRate, releaseMs);
    // Half of the reduction at 0 dBFS comes back as make-up gain.
    const float reductionAtFullScale = -m_threshold * (1.0f - 1.0f / m_ratio);
    m_makeup = dbToGain(std::max(0.0f, reductionAtFullScale * 0.5f));
}

void Compressor::process(float* x, int frames, int channels) noexcept {
    constexpr float kKnee = 6.0f;
    float maxGr = 0.0f;
    for (int i = 0; i < frames; ++i) {
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) peak = std::max(peak, std::fabs(x[i * channels + c]));
        const float coeff = peak > m_env ? m_attack : m_release;
        m_env = coeff * m_env + (1.0f - coeff) * peak;
        const float levelDb = gainToDb(m_env);
        const float over = levelDb - m_threshold;
        float grDb = 0.0f;
        if (over > kKnee / 2) grDb = over * (1.0f - 1.0f / m_ratio);
        else if (over > -kKnee / 2) { const float k = over + kKnee / 2; grDb = (1.0f - 1.0f / m_ratio) * k * k / (2.0f * kKnee); }
        maxGr = std::max(maxGr, grDb);
        const float g = dbToGain(-grDb) * m_makeup;
        for (int c = 0; c < channels; ++c) x[i * channels + c] *= g;
    }
    m_lastGrDb = maxGr;
}

// ---------------------------------------------------------------- gate

void Gate::set(double sampleRate, float thresholdDb) {
    m_threshold = dbToGain(thresholdDb);
    m_open = coefficient(sampleRate, 5.0f);
    m_close = coefficient(sampleRate, 80.0f);
    m_envRelease = coefficient(sampleRate, 30.0f);
}

void Gate::process(float* x, int frames, int channels) noexcept {
    for (int i = 0; i < frames; ++i) {
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) peak = std::max(peak, std::fabs(x[i * channels + c]));
        m_env = peak > m_env ? peak : m_envRelease * m_env + (1.0f - m_envRelease) * peak;
        const float target = m_env >= m_threshold ? 1.0f : 0.0f;
        const float coeff = target > m_gain ? m_open : m_close;
        m_gain = coeff * m_gain + (1.0f - coeff) * target;
        for (int c = 0; c < channels; ++c) x[i * channels + c] *= m_gain;
    }
}

// ---------------------------------------------------------------- limiter

void Limiter::set(double sampleRate, float ceilingDb) {
    m_ceiling = dbToGain(ceilingDb);
    m_release = coefficient(sampleRate, 50.0f);
}

void Limiter::process(float* x, int frames, int channels) noexcept {
    for (int i = 0; i < frames; ++i) {
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) peak = std::max(peak, std::fabs(x[i * channels + c]));
        const float needed = peak * m_gain > m_ceiling ? m_ceiling / std::max(peak, 1e-9f) : 1.0f;
        m_gain = needed < m_gain ? needed : m_release * m_gain + (1.0f - m_release) * 1.0f;
        if (peak * m_gain > m_ceiling) m_gain = m_ceiling / peak;   // instant attack
        for (int c = 0; c < channels; ++c) x[i * channels + c] *= m_gain;
    }
}

}  // namespace lf::playback::dsp
