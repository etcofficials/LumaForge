#include "lumaforge/project/TimelineCommands.h"

#include <algorithm>

namespace lf::project {

// ---------------------------------------------------------------- SequenceEdit

std::vector<ClipId> SequenceEdit::clipIds() const {
    std::vector<ClipId> ids;
    for (const auto& c : added) ids.push_back(c.id);
    for (auto id : removed) ids.push_back(id);
    for (const auto& c : modified) ids.push_back(c.id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

void SequenceEdit::modify(Clip c) {
    for (auto& m : modified) if (m.id == c.id) { m = std::move(c); return; }
    // A clip added in this same edit is simply updated in place.
    for (auto& a : added) if (a.id == c.id) { a = std::move(c); return; }
    modified.push_back(std::move(c));
}

void SequenceEdit::remove(ClipId id) {
    std::erase_if(added, [id](const Clip& c) { return c.id == id; });
    std::erase_if(modified, [id](const Clip& c) { return c.id == id; });
    if (std::find(removed.begin(), removed.end(), id) == removed.end()) removed.push_back(id);
}

// ---------------------------------------------------------------- command

SequenceEditCommand::SequenceEditCommand(Document& doc, std::string text, SequenceEdit edit, int mergeId)
    : m_doc(doc), m_text(std::move(text)), m_edit(std::move(edit)), m_mergeId(mergeId) {}

void SequenceEditCommand::redo() {
    auto& seq = m_doc.project().sequence;
    if (!m_captured) {
        m_captured = true;
        for (auto id : m_edit.removed) {
            for (std::size_t i = 0; i < seq.clips.size(); ++i) if (seq.clips[i].id == id) { m_removedBefore.emplace_back(i, seq.clips[i]); break; }
        }
        for (const auto& m : m_edit.modified) if (const auto* c = seq.findClip(m.id)) m_modifiedBefore.push_back(*c);
        for (const auto& t : m_edit.tracks) if (const auto* o = seq.findTrack(t.id)) m_tracksBefore.push_back(*o);
    }
    // Remove (highest index first keeps the recorded indices meaningful for undo)
    for (auto id : m_edit.removed) std::erase_if(seq.clips, [id](const Clip& c) { return c.id == id; });
    for (const auto& m : m_edit.modified) if (auto* c = seq.findClip(m.id)) *c = m;
    for (const auto& a : m_edit.added) seq.clips.push_back(a);
    for (const auto& t : m_edit.tracks) if (auto* o = seq.findTrack(t.id)) *o = t;
    notify(!m_edit.tracks.empty());
}

void SequenceEditCommand::undo() {
    auto& seq = m_doc.project().sequence;
    for (const auto& a : m_edit.added) std::erase_if(seq.clips, [&](const Clip& c) { return c.id == a.id; });
    for (const auto& b : m_modifiedBefore) if (auto* c = seq.findClip(b.id)) *c = b;
    // Re-insert removed clips at their original positions (ascending index).
    auto removed = m_removedBefore;
    std::sort(removed.begin(), removed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [index, clip] : removed) seq.clips.insert(seq.clips.begin() + static_cast<std::ptrdiff_t>(std::min(index, seq.clips.size())), clip);
    for (const auto& t : m_tracksBefore) if (auto* o = seq.findTrack(t.id)) *o = t;
    notify(!m_edit.tracks.empty());
}

bool SequenceEditCommand::mergeWith(const cmd::Command& next) {
    if (m_mergeId == mergeIds::None || next.mergeId() != m_mergeId) return false;
    const auto* other = dynamic_cast<const SequenceEditCommand*>(&next);
    if (!other) return false;
    // Only pure modifications of the same clips coalesce.
    if (!m_edit.added.empty() || !m_edit.removed.empty() || !other->m_edit.added.empty() || !other->m_edit.removed.empty()) return false;
    if (!m_edit.tracks.empty() || !other->m_edit.tracks.empty()) return false;
    if (m_edit.modified.size() != other->m_edit.modified.size()) return false;
    for (const auto& m : other->m_edit.modified) {
        const bool known = std::any_of(m_edit.modified.begin(), m_edit.modified.end(), [&](const Clip& c) { return c.id == m.id; });
        if (!known) return false;
    }
    // Keep our "before" state, adopt their "after" state. The stack has
    // already executed `next`, so the document is up to date.
    for (const auto& m : other->m_edit.modified) m_edit.modify(m);
    return true;
}

void SequenceEditCommand::notify(bool tracksToo) {
    if (tracksToo) m_doc.notify(Change{Change::Kind::TracksChanged, {}, {}});
    m_doc.notify(Change{Change::Kind::ClipsChanged, {}, m_edit.clipIds()});
}

}  // namespace lf::project
