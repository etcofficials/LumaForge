#include "lumaforge/project/Sequence.h"

#include <algorithm>

namespace lf::project {

std::vector<const Clip*> Sequence::clipsOnTrack(TrackId id) const {
    std::vector<const Clip*> out;
    for (const auto& c : clips) if (c.track == id) out.push_back(&c);
    std::sort(out.begin(), out.end(), [](const Clip* a, const Clip* b) { return a->start < b->start; });
    return out;
}

const Clip* Sequence::clipAt(TrackId id, TimePoint t) const noexcept {
    for (const auto& c : clips) if (c.track == id && c.contains(t)) return &c;
    return nullptr;
}

Duration Sequence::duration() const noexcept {
    Ticks end = 0;
    for (const auto& c : clips) end = std::max(end, c.end().ticks);
    return Duration{end};
}

std::vector<const Track*> Sequence::videoTracksBottomUp() const {
    std::vector<const Track*> out;
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) if (it->kind == TrackKind::Video) out.push_back(&*it);
    return out;
}

std::vector<const Track*> Sequence::audioTracks() const {
    std::vector<const Track*> out;
    for (const auto& t : tracks) if (t.kind == TrackKind::Audio) out.push_back(&t);
    return out;
}

const Track* Sequence::primaryTrack(TrackKind kind) const noexcept {
    if (kind == TrackKind::Video) {
        const Track* last = nullptr;
        for (const auto& t : tracks) if (t.kind == TrackKind::Video) last = &t;
        return last;
    }
    for (const auto& t : tracks) if (t.kind == TrackKind::Audio) return &t;
    return nullptr;
}

Sequence Sequence::withDefaultTracks() {
    Sequence s;
    s.tracks = {
        {TrackId{1}, TrackKind::Video, "V2"},
        {TrackId{2}, TrackKind::Video, "V1"},
        {TrackId{3}, TrackKind::Audio, "A1"},
        {TrackId{4}, TrackKind::Audio, "A2"},
    };
    return s;
}

}  // namespace lf::project
