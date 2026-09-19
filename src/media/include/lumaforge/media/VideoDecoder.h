#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/media/MediaInfo.h"
#include "lumaforge/media/VideoFrame.h"

#include <memory>
#include <optional>
#include <string>

namespace lf::hw { class GpuDevice; }

namespace lf::media {

// Demuxes one file's video stream and decodes it, hardware-accelerated
// (D3D11VA on the shared GpuDevice) when the caller allows it and the driver
// accepts the stream, software otherwise. Frames come out in presentation
// order with timestamps in flicks (media time, 0 = start of file).
//
// One instance is used from one thread at a time (the owning reader thread).
class VideoDecoder {
public:
    struct Options {
        std::shared_ptr<hw::GpuDevice> gpu;   // required for hardware decode
        bool allowHardware = true;
        int softwareThreads = 0;              // 0 = auto (FFmpeg picks, capped by us)
    };

    static Result<std::unique_ptr<VideoDecoder>> open(const std::string& utf8Path, const Options& options);
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Positions the decoder so that the next frames lead up to `t` (seeks to
    // the preceding keyframe and flushes). Frames before `t` are still
    // returned by next(); callers skip them. Cheap when `t` is just ahead.
    Status seek(TimePoint t);

    // Next frame in presentation order, or nullopt at end of stream.
    Result<std::optional<VideoFrame>> next();

    [[nodiscard]] bool isHardware() const noexcept { return m_hardware; }
    [[nodiscard]] const VideoStreamInfo& info() const noexcept { return m_info; }
    [[nodiscard]] Duration duration() const noexcept { return m_duration; }
    [[nodiscard]] Duration frameDuration() const noexcept;
    [[nodiscard]] const std::string& path() const noexcept { return m_path; }

private:
    VideoDecoder() = default;
    Status openStream(const Options& options, bool hardware);
    Status pump();   // feeds packets until the decoder yields a frame or EOF

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::string m_path;
    VideoStreamInfo m_info;
    Duration m_duration;
    bool m_hardware = false;
    bool m_eof = false;
};

}  // namespace lf::media
