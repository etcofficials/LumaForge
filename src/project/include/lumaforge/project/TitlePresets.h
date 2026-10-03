#pragma once

#include "lumaforge/project/Sequence.h"

#include <span>

namespace lf::project {

// Ready-made looks for text clips. A preset only initialises properties
// (font, size, box, position); everything stays editable afterwards.
struct TitlePreset {
    const char* id;            // stable key
    const char* name;          // UI label
    const char* description;   // one line for tooltips
    const char* sampleText;    // text a new clip starts with
};

[[nodiscard]] std::span<const TitlePreset> titlePresets() noexcept;

// Applies preset `index` (into titlePresets()) to a text clip for a frame of
// frameWidth x frameHeight. Text the user already typed is kept unless it is
// still the default "Title"; `keepText = false` always uses the sample text.
void applyTitlePreset(Clip& clip, int index, int frameWidth, int frameHeight, bool keepText = true);

// A new text clip with the preset applied (id/track/start left to the caller).
[[nodiscard]] Clip makeTitleClip(int index, int frameWidth, int frameHeight);

}  // namespace lf::project
