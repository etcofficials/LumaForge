#include "lumaforge/project/Document.h"

#include <algorithm>

namespace lf::project {

Document::Document() = default;

int Document::addListener(Listener listener) {
    const int token = m_nextToken++;
    m_listeners.emplace_back(token, std::move(listener));
    return token;
}

void Document::removeListener(int token) {
    std::erase_if(m_listeners, [token](const auto& p) { return p.first == token; });
}

void Document::notify(const Change& change) {
    // Copy: a listener may add/remove listeners while being notified.
    const auto listeners = m_listeners;
    for (const auto& [token, fn] : listeners) fn(change);
}

MediaId Document::allocateMediaId() { return static_cast<MediaId>(m_nextMediaId++); }
ClipId Document::allocateClipId() { return static_cast<ClipId>(m_nextClipId++); }
TrackId Document::allocateTrackId() { return static_cast<TrackId>(m_nextTrackId++); }

void Document::noteIdsInUse() {
    for (const auto& m : m_project.media) m_nextMediaId = std::max(m_nextMediaId, raw(m.id) + 1);
}

void Document::reset(Project project) {
    m_project = std::move(project);
    m_undo.clear();
    m_nextMediaId = m_nextClipId = m_nextTrackId = 1;
    noteIdsInUse();
    notify(Change{Change::Kind::Reset, {}, {}});
}

}  // namespace lf::project
