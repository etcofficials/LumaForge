#pragma once

#include "lumaforge/core/Time.h"
#include "lumaforge/project/Project.h"

#include <span>
#include <string>
#include <vector>

namespace lf::exporting {

enum class Container { Mp4, Mov, Mkv };
enum class VideoCodec { H264, Hevc };
enum class RateControl { Quality, Bitrate };   // constant quality (CRF) or average bitrate

// Everything that defines one render to file. Defaults are a sensible
// high-quality H.264/AAC MP4 at the project's size.
struct ExportSettings {
    std::string outputPath;              // UTF-8, absolute; extension follows the container
    Container container = Container::Mp4;
    VideoCodec codec = VideoCodec::H264;
    std::string encoder;                 // FFmpeg encoder name; empty = libx264 / libx265
    int width = 0;                       // 0 = project size (height then follows)
    int height = 0;
    RateControl rateControl = RateControl::Quality;
    int crf = 20;                        // quality mode: lower is better (x264 18 ~ visually lossless)
    int videoKbps = 12000;               // bitrate mode
    std::string speedPreset = "medium";  // x264/x265: ultrafast ... veryslow
    bool includeAudio = true;
    int audioKbps = 192;
    int audioSampleRate = 48000;
    TimePoint rangeStart{0};             // sequence time
    TimePoint rangeEnd{-1};              // -1 = end of the sequence
    bool fastStart = true;               // MP4/MOV: index at the front, plays while downloading
};

struct ExportPreset {
    const char* id;
    const char* name;
    const char* description;
    Container container;
    VideoCodec codec;
    int height;          // 0 = project size; width follows the project's aspect
    RateControl rateControl;
    int crf;
    int videoKbps;
    const char* speedPreset;
    int audioKbps;
};

[[nodiscard]] std::span<const ExportPreset> exportPresets() noexcept;
// Applies a preset's choices to `s` for this project (keeps path and range).
void applyPreset(ExportSettings& s, const ExportPreset& preset, const project::Project& project);

[[nodiscard]] const char* containerExtension(Container c) noexcept;   // ".mp4"
[[nodiscard]] const char* containerName(Container c) noexcept;        // "MP4"
[[nodiscard]] const char* defaultEncoder(VideoCodec c) noexcept;      // "libx264"
// Whether this FFmpeg build has the encoder (software encoders: also usable;
// hardware encoders still need the hardware probe to confirm).
[[nodiscard]] bool encoderAvailable(const std::string& ffmpegName) noexcept;

// The output size: settings' size, or the project's; both even (4:2:0).
void outputSize(const ExportSettings& s, const project::Project& project, int& width, int& height);
// The range actually rendered, clamped to the sequence.
[[nodiscard]] TimeRange exportRange(const ExportSettings& s, const project::Project& project);
[[nodiscard]] std::int64_t frameCount(const ExportSettings& s, const project::Project& project);
// Rough file size in bytes (bitrate mode exact-ish; quality mode an estimate).
[[nodiscard]] std::uint64_t estimatedBytes(const ExportSettings& s, const project::Project& project);

// Problems that prevent the export (empty = fine): nothing to render, bad
// path, unsupported combinations. Offline media is reported separately.
[[nodiscard]] std::vector<std::string> validate(const ExportSettings& s, const project::Project& project);
// Display names of media used in the range whose files are missing.
[[nodiscard]] std::vector<std::string> offlineMediaInRange(const ExportSettings& s, const project::Project& project);

}  // namespace lf::exporting
