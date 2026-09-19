#pragma once

#include "lumaforge/core/Rational.h"
#include "lumaforge/project/MediaItem.h"

#include <string>
#include <vector>

namespace lf::project {

struct ProjectSettings {
    std::string name = "Untitled";
    int width = 1920;
    int height = 1080;
    Rational frameRate = fps::f30;
    int audioSampleRate = 48000;
};

// The complete editable state of a project. Plain data: no behaviour, no
// Qt, trivially serialisable. The Sequence (tracks + clips) is added by the
// timeline module in the next phase.
struct Project {
    ProjectSettings settings;
    std::vector<MediaItem> media;

    [[nodiscard]] const MediaItem* findMedia(MediaId id) const noexcept {
        for (const auto& m : media) if (m.id == id) return &m;
        return nullptr;
    }
    [[nodiscard]] MediaItem* findMedia(MediaId id) noexcept {
        for (auto& m : media) if (m.id == id) return &m;
        return nullptr;
    }
    [[nodiscard]] const MediaItem* findMediaByPath(const std::string& path) const noexcept {
        for (const auto& m : media) if (m.path == path) return &m;
        return nullptr;
    }
};

}  // namespace lf::project
