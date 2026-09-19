#pragma once

#include "lumaforge/core/Rational.h"
#include "lumaforge/core/Time.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lf::media {

enum class MediaType { Video, Audio, Image };

[[nodiscard]] constexpr const char* mediaTypeName(MediaType t) noexcept {
    switch (t) {
        case MediaType::Video: return "Video";
        case MediaType::Audio: return "Audio";
        case MediaType::Image: return "Image";
    }
    return "?";
}

struct VideoStreamInfo {
    int streamIndex = -1;
    std::string codecName;      // "h264"
    std::string codecLongName;  // "H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"
    std::string profile;        // "High"
    int width = 0;
    int height = 0;
    Rational frameRate;         // best guess (avg_frame_rate, else r_frame_rate)
    Rational timeBase;
    Rational sampleAspectRatio{1, 1};
    std::string pixelFormat;    // "yuv420p"
    int bitDepth = 8;
    std::int64_t bitRate = 0;
    int rotationDegrees = 0;    // from display matrix side data
    bool isFullRange = false;
    std::int64_t frameCount = 0;  // 0 when unknown
};

struct AudioStreamInfo {
    int streamIndex = -1;
    std::string codecName;      // "aac"
    std::string codecLongName;
    int channels = 0;
    std::string channelLayout;  // "stereo"
    int sampleRate = 0;
    std::string sampleFormat;   // "fltp"
    std::int64_t bitRate = 0;
    Rational timeBase;
};

struct MediaInfo {
    std::string path;           // UTF-8, absolute
    std::string fileName;
    std::string containerName;  // "mov,mp4,m4a,3gp,3g2,mj2"
    std::string containerLongName;
    std::uint64_t fileSize = 0;
    std::int64_t modifiedTimeUnix = 0;
    MediaType type = MediaType::Video;
    Duration duration;          // container duration; images report 0
    std::int64_t bitRate = 0;
    std::optional<VideoStreamInfo> video;
    std::optional<AudioStreamInfo> audio;

    [[nodiscard]] bool hasVideo() const noexcept { return video.has_value(); }
    [[nodiscard]] bool hasAudio() const noexcept { return audio.has_value(); }
};

}  // namespace lf::media
