#pragma once

#include "lumaforge/playback/Program.h"
#include "lumaforge/project/Project.h"

#include <unordered_map>

namespace lf::playback {

// Plays the project's sequence. Built from a snapshot of the project on the
// UI thread whenever the document changes; the engine swaps programs
// atomically, keeping readers for media that is still in use.
class SequenceProgram final : public Program {
public:
    explicit SequenceProgram(const project::Project& project);

    [[nodiscard]] Duration duration() const override { return m_duration; }
    [[nodiscard]] Rational frameRate() const override { return m_frameRate; }
    [[nodiscard]] int frameWidth() const override { return m_width; }
    [[nodiscard]] int frameHeight() const override { return m_height; }
    [[nodiscard]] bool hasAudio() const override { return m_hasAudio; }
    void videoLayersAt(TimePoint t, std::vector<VideoLayerRequest>& out) const override;
    void audioClipsAt(TimePoint t, std::vector<AudioClipRequest>& out) const override;
    [[nodiscard]] std::vector<std::string> mediaPaths() const override;

    // Gain including fades and mute for a clip at sequence time t.
    static float gainAt(const project::Clip& clip, TimePoint t) noexcept;

private:
    struct VisualEntry {
        project::Clip clip;
        std::string path;
        bool isImage = false;
    };
    struct AudioEntry {
        project::Clip clip;
        std::string path;
    };
    // Visual clips grouped per video track, bottom-up; each list sorted by start.
    std::vector<std::vector<VisualEntry>> m_videoTracks;
    std::vector<AudioEntry> m_audioClips;
    std::vector<std::string> m_paths;
    Duration m_duration;
    Rational m_frameRate;
    int m_width = 1920, m_height = 1080;
    bool m_hasAudio = false;
};

}  // namespace lf::playback
