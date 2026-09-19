#pragma once

#include "lumaforge/core/Time.h"
#include "lumaforge/media/VideoDecoder.h"
#include "lumaforge/rendering/FrameTexture.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace lf::hw { class GpuDevice; }

namespace lf::playback {

// Decodes one media file on its own thread, a few frames ahead of the time
// the engine asks for, and hands back GPU-ready FrameTextures. Seeks are
// coalesced: only the latest requested time matters, so scrubbing never
// queues stale work. Decoder surfaces are released as soon as the frame has
// been copied into the texture pool.
class VideoReader {
public:
    struct Options {
        std::shared_ptr<hw::GpuDevice> gpu;
        std::shared_ptr<render::FrameTexturePool> pool;
        bool allowHardware = true;
        bool isImage = false;
        int prefetchFrames = 4;
    };

    VideoReader(std::string path, Options options);
    ~VideoReader();

    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;

    // Best available frame for media time t (never blocks). `exact` reports
    // whether the frame actually covers t; when false the caller got the
    // closest thing available (the reader is catching up).
    std::shared_ptr<render::FrameTexture> frameAt(TimePoint t, bool& exact);

    // Tell the reader where playback is heading so it decodes ahead.
    void hint(TimePoint t);

    [[nodiscard]] bool isHardware() const noexcept { return m_hardware.load(); }
    [[nodiscard]] bool hasError() const noexcept { return m_failed.load(); }
    [[nodiscard]] std::string errorText() const;
    [[nodiscard]] const std::string& path() const noexcept { return m_path; }
    [[nodiscard]] Duration frameDuration() const noexcept { return m_frameDuration; }
    [[nodiscard]] std::uint64_t decodedFrames() const noexcept { return m_decoded.load(); }

private:
    struct Entry {
        std::shared_ptr<render::FrameTexture> texture;
        TimePoint pts;
        Duration duration;
    };
    void threadMain();
    bool openDecoder();
    void decodeImage();

    std::string m_path;
    Options m_options;
    std::unique_ptr<media::VideoDecoder> m_decoder;
    Duration m_frameDuration{lf::frameDuration(fps::f30)};
    std::atomic<bool> m_hardware{false};
    std::atomic<bool> m_failed{false};
    std::atomic<std::uint64_t> m_decoded{0};
    std::string m_error;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Entry> m_queue;         // ascending pts
    std::shared_ptr<render::FrameTexture> m_lastReturned;
    TimePoint m_lastReturnedPts{-1};
    TimePoint m_target;                // latest time the consumer wants
    bool m_targetChanged = false;
    bool m_stop = false;
    std::thread m_thread;
    TimePoint m_decodePos{-1};         // pts of the last decoded frame
    bool m_eof = false;
};

}  // namespace lf::playback
