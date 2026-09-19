#pragma once

#include "lumaforge/media/MediaInfo.h"
#include "lumaforge/playback/Program.h"

namespace lf::playback {

// Plays one media file from start to end: the "source monitor" program used
// when a media item is opened from the bin.
class SourceProgram final : public Program {
public:
    SourceProgram(media::MediaInfo info, int frameWidth, int frameHeight, Rational frameRate);

    [[nodiscard]] Duration duration() const override { return m_duration; }
    [[nodiscard]] Rational frameRate() const override { return m_frameRate; }
    [[nodiscard]] int frameWidth() const override { return m_width; }
    [[nodiscard]] int frameHeight() const override { return m_height; }
    [[nodiscard]] bool hasAudio() const override { return m_info.hasAudio(); }
    void videoLayersAt(TimePoint t, std::vector<VideoLayerRequest>& out) const override;
    void audioClipsAt(TimePoint t, std::vector<AudioClipRequest>& out) const override;
    [[nodiscard]] std::vector<std::string> mediaPaths() const override { return {m_info.path}; }
    [[nodiscard]] const media::MediaInfo& info() const noexcept { return m_info; }

private:
    media::MediaInfo m_info;
    Duration m_duration;
    Rational m_frameRate;
    int m_width, m_height;
};

}  // namespace lf::playback
