#pragma once

#include "lumaforge/core/Time.h"

#include <string>
#include <vector>

// Keyframe animation for clip properties. Keys are placed in *media* time
// (the clip's source time), so trimming, splitting, moving and changing the
// speed of a clip keep the animation attached to the same picture or sound.
namespace lf::project {

enum class AnimProp { PositionX, PositionY, ScaleX, ScaleY, Rotation, Opacity, CropLeft, CropRight, CropTop, CropBottom, Volume, Count };

[[nodiscard]] const char* animPropId(AnimProp prop) noexcept;              // stable key in project files
[[nodiscard]] AnimProp animPropFromId(const std::string& id) noexcept;     // Count when unknown

// How the value travels from this key to the next one.
enum class Interp { Hold, Linear, Smooth };

[[nodiscard]] const char* interpId(Interp interp) noexcept;
[[nodiscard]] Interp interpFromId(const std::string& id) noexcept;   // Linear when unknown

struct Keyframe {
    TimePoint time;          // media time
    float value = 0.0f;
    Interp interp = Interp::Linear;
    friend constexpr bool operator==(const Keyframe&, const Keyframe&) = default;
};

struct KeyTrack {
    AnimProp prop = AnimProp::PositionX;
    std::vector<Keyframe> keys;   // sorted by time, unique times, never empty in a clip
    friend bool operator==(const KeyTrack&, const KeyTrack&) = default;
};

// Value of a key list at media time t: constant before the first key and
// after the last; between keys the left key's interpolation decides.
[[nodiscard]] float evaluate(const std::vector<Keyframe>& keys, TimePoint t) noexcept;

}  // namespace lf::project
