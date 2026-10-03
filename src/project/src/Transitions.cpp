#include "lumaforge/project/Transitions.h"

#include "lumaforge/project/Sequence.h"

#include <algorithm>
#include <iterator>

namespace lf::project {

namespace {
constexpr TransitionInfo kInfo[] = {
    {"crossDissolve", "Cross Dissolve", "Dissolves", "Blends the outgoing picture into the incoming one"},
    {"dipToBlack",    "Dip to Black",   "Dissolves", "Fades out to black, then in from black"},
    {"dipToWhite",    "Dip to White",   "Dissolves", "Fades out to white, then in from white"},
    {"wipeLeft",      "Wipe Left",      "Wipes",     "The incoming picture is revealed from right to left"},
    {"wipeRight",     "Wipe Right",     "Wipes",     "The incoming picture is revealed from left to right"},
    {"wipeUp",        "Wipe Up",        "Wipes",     "The incoming picture is revealed from bottom to top"},
    {"wipeDown",      "Wipe Down",      "Wipes",     "The incoming picture is revealed from top to bottom"},
    {"slideLeft",     "Slide Left",     "Motion",    "The incoming picture slides in from the right"},
    {"slideRight",    "Slide Right",    "Motion",    "The incoming picture slides in from the left"},
    {"pushLeft",      "Push Left",      "Motion",    "The incoming picture pushes the outgoing one out to the left"},
    {"pushRight",     "Push Right",     "Motion",    "The incoming picture pushes the outgoing one out to the right"},
    {"zoom",          "Zoom",           "Motion",    "The outgoing picture zooms towards the viewer as it fades away"},
};
static_assert(std::size(kInfo) == static_cast<std::size_t>(TransitionType::Count));
}  // namespace

const TransitionInfo& transitionInfo(TransitionType type) noexcept {
    const auto i = static_cast<std::size_t>(type);
    return kInfo[i < std::size(kInfo) ? i : 0];
}

TransitionType transitionTypeFromId(const std::string& id) noexcept {
    for (std::size_t i = 0; i < std::size(kInfo); ++i) if (id == kInfo[i].id) return static_cast<TransitionType>(i);
    return TransitionType::Count;
}

const Clip* touchingNeighbour(const std::vector<const Clip*>& clips, const Clip& clip, bool atStart) {
    for (const Clip* c : clips) {
        if (c->id == clip.id) continue;
        if (atStart && c->end() == clip.start) return c;
        if (!atStart && c->start == clip.end()) return c;
    }
    return nullptr;
}

Duration maxTransitionDuration(const std::vector<const Clip*>& clips, const Clip& clip, bool atStart) {
    if (atStart) {
        if (const Clip* prev = touchingNeighbour(clips, clip, true)) return Duration{2 * std::min(prev->duration.ticks, clip.duration.ticks)};
        return clip.duration;
    }
    return clip.duration;
}

std::vector<TransitionWindow> transitionWindows(const std::vector<const Clip*>& clips) {
    std::vector<TransitionWindow> out;
    for (const Clip* c : clips) {
        if (c->transitionIn) {
            TransitionWindow w;
            w.transition = *c->transitionIn;
            w.owner = c;
            w.atStart = true;
            w.to = c;
            if (const Clip* prev = touchingNeighbour(clips, *c, true)) {
                // Centred on the cut; half on each side, never beyond either clip.
                const Ticks half = std::min({w.transition.duration.ticks / 2, prev->duration.ticks, c->duration.ticks});
                w.from = prev;
                w.start = TimePoint{c->start.ticks - half};
                w.end = TimePoint{c->start.ticks + half};
            } else {
                w.start = c->start;
                w.end = TimePoint{c->start.ticks + std::min(w.transition.duration.ticks, c->duration.ticks)};
            }
            if (w.end > w.start) out.push_back(w);
        }
        if (c->transitionOut && !touchingNeighbour(clips, *c, false)) {
            TransitionWindow w;
            w.transition = *c->transitionOut;
            w.owner = c;
            w.atStart = false;
            w.from = c;
            w.start = TimePoint{c->end().ticks - std::min(w.transition.duration.ticks, c->duration.ticks)};
            w.end = c->end();
            if (w.end > w.start) out.push_back(w);
        }
    }
    std::sort(out.begin(), out.end(), [](const TransitionWindow& a, const TransitionWindow& b) { return a.start < b.start; });
    return out;
}

}  // namespace lf::project
