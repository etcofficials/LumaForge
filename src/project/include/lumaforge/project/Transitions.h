#pragma once

#include "lumaforge/core/Time.h"

#include <optional>
#include <string>

// Transitions between clips on a video track. A clip may carry a transition
// at its start (`transitionIn`) and at its end (`transitionOut`):
//  - transitionIn on a clip that touches the previous clip on its track is a
//    transition across that cut, centred on it (the outgoing clip continues
//    into its media handle, or holds its last frame when there is none);
//  - transitionIn with no clip touching before it transitions from whatever
//    is underneath (lower tracks / black) inside the clip's first frames;
//  - transitionOut applies only when no clip touches the clip's end: it
//    transitions to whatever is underneath inside the clip's last frames.
namespace lf::project {

enum class TransitionType {
    CrossDissolve, DipToBlack, DipToWhite, WipeLeft, WipeRight, WipeUp, WipeDown,
    SlideLeft, SlideRight, PushLeft, PushRight, Zoom,
    Count
};

struct TransitionInfo {
    const char* id;            // stable key in project files
    const char* name;          // UI label
    const char* category;      // browser grouping
    const char* description;
};
[[nodiscard]] const TransitionInfo& transitionInfo(TransitionType type) noexcept;
[[nodiscard]] TransitionType transitionTypeFromId(const std::string& id) noexcept;   // Count when unknown

inline constexpr Duration kDefaultTransitionDuration = Duration{kFlicksPerSecond};   // 1 s

struct Transition {
    TransitionType type = TransitionType::CrossDissolve;
    Duration duration = kDefaultTransitionDuration;
    friend constexpr bool operator==(const Transition&, const Transition&) = default;
};

}  // namespace lf::project
