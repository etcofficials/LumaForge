#pragma once

// FFmpeg encoding/muxing for the exporter (internal).

#include "lumaforge/core/Rational.h"
#include "lumaforge/core/Result.h"
#include "lumaforge/export/ExportSettings.h"

#include <cstdint>
#include <memory>
#include <string>

namespace lf::exporting {

class Muxer {
public:
    struct Spec {
        std::string path;           // file actually written (the .partial name)
        ExportSettings settings;
        int width = 0, height = 0;
        Rational frameRate;
        bool audio = false;
        int audioChannels = 2;
        std::string title;
        std::string comment;
    };

    static Result<std::unique_ptr<Muxer>> open(const Spec& spec);
    ~Muxer();

    // One BGRA frame (stride bytes per row); frames are numbered from 0.
    Status writeVideo(const std::uint8_t* bgra, int stride, std::int64_t frameIndex);
    // Interleaved float samples at the spec's rate/channels, in order.
    Status writeAudio(const float* interleaved, int frames);
    // Flushes encoders and writes the trailer.
    Status finish();

    [[nodiscard]] const std::string& videoEncoderName() const noexcept { return m_videoEncoder; }
    [[nodiscard]] const std::string& audioEncoderName() const noexcept { return m_audioEncoder; }

private:
    Muxer();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::string m_videoEncoder;
    std::string m_audioEncoder;
};

}  // namespace lf::exporting
