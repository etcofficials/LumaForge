#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace lf::audio {

// WASAPI shared-mode output. Runs its own high-priority render thread that
// calls `render` whenever the device wants more samples. The callback must
// fill every requested frame (write zeros for silence) and must not block.
class AudioOutput {
public:
    // Fill `frames` interleaved float frames (channels() per frame).
    using RenderCallback = std::function<void(float* interleaved, int frames)>;

    struct Options {
        int sampleRate = 48000;
        int channels = 2;
        int bufferMilliseconds = 40;
    };

    static Result<std::unique_ptr<AudioOutput>> create(const Options& options = {});
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    Status start(RenderCallback render);
    void stop();
    [[nodiscard]] bool isRunning() const noexcept { return m_running.load(); }

    [[nodiscard]] int sampleRate() const noexcept { return m_sampleRate; }
    [[nodiscard]] int channels() const noexcept { return m_channels; }
    [[nodiscard]] Duration bufferDuration() const noexcept { return Duration{ticksFromTimeBase(m_bufferFrames, Rational{1, m_sampleRate})}; }
    [[nodiscard]] std::string deviceName() const { return m_deviceName; }

private:
    AudioOutput() = default;
    void renderThread();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    RenderCallback m_render;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    int m_sampleRate = 48000;
    int m_channels = 2;
    std::uint32_t m_bufferFrames = 0;
    std::string m_deviceName;
};

}  // namespace lf::audio
