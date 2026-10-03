#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace lf::audio {

// Records the default capture device (microphone) through WASAPI shared mode
// into a WAV file (48 kHz stereo 16-bit PCM; Windows converts from the
// device format). A capture thread writes as data arrives, so recordings are
// limited only by disk space; the file is valid after stop().
class AudioRecorder {
public:
    static Result<std::unique_ptr<AudioRecorder>> create();   // opens the default microphone
    ~AudioRecorder();

    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    Status start(const std::string& utf8WavPath);
    // Stops and finalises the WAV header. Returns the recorded length.
    Result<Duration> stop();
    [[nodiscard]] bool isRecording() const noexcept { return m_running.load(); }

    [[nodiscard]] std::string deviceName() const { return m_deviceName; }
    // Peak of the most recent audio (linear 0..1); the UI polls it for a meter.
    [[nodiscard]] float takePeak() noexcept { return m_peak.exchange(0.0f); }
    [[nodiscard]] Duration recorded() const noexcept { return Duration{ticksFromTimeBase(m_frames.load(), Rational{1, kRate})}; }

    static constexpr int kRate = 48000;
    static constexpr int kChannels = 2;

private:
    AudioRecorder() = default;
    void captureThread();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<float> m_peak{0.0f};
    std::atomic<std::int64_t> m_frames{0};
    std::string m_deviceName;
    std::string m_error;
};

}  // namespace lf::audio
