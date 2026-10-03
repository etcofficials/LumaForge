#pragma once

#include "lumaforge/commands/Command.h"
#include "lumaforge/project/Document.h"

#include <optional>
#include <string>
#include <vector>

namespace lf::project {

// A set of changes to the sequence, applied atomically by SequenceEditCommand.
// Every timeline operation (drop, move, trim, split, delete, property edit)
// is expressed as one of these, so there is exactly one undoable command type
// for the timeline and the inverse is computed generically.
struct SequenceMarks {
    TimePoint in{-1};
    TimePoint out{-1};
    friend constexpr bool operator==(const SequenceMarks&, const SequenceMarks&) = default;
};

struct SequenceEdit {
    std::vector<Clip> added;        // inserted as given (ids pre-allocated)
    std::vector<ClipId> removed;
    std::vector<Clip> modified;     // complete new state of existing clips
    std::vector<Track> tracks;      // complete new state of existing tracks
    std::optional<SequenceMarks> marks;   // new In/Out points
    // Structural track changes. Applied in this order: remove, insert, reorder, then `tracks`.
    std::vector<TrackId> tracksRemoved;
    std::vector<std::pair<std::size_t, Track>> tracksAdded;   // display index at insertion time + track
    std::optional<std::vector<TrackId>> trackOrder;           // complete new display order

    [[nodiscard]] bool touchesTracks() const noexcept { return !tracks.empty() || !tracksRemoved.empty() || !tracksAdded.empty() || trackOrder.has_value(); }
    [[nodiscard]] bool empty() const noexcept { return added.empty() && removed.empty() && modified.empty() && !touchesTracks() && !marks; }
    // Affected clip ids (for change notifications and selection).
    [[nodiscard]] std::vector<ClipId> clipIds() const;
    void add(Clip c) { added.push_back(std::move(c)); }
    void modify(Clip c);            // replaces an earlier entry for the same id
    void remove(ClipId id);
};

// Merge ids for coalescing continuous edits (drag, spin box) into one step.
namespace mergeIds {
inline constexpr int None = -1;
inline constexpr int ClipProperty = 100;   // + per-property offset chosen by the caller
}  // namespace mergeIds

class SequenceEditCommand final : public cmd::Command {
public:
    SequenceEditCommand(Document& doc, std::string text, SequenceEdit edit, int mergeId = mergeIds::None);
    [[nodiscard]] std::string text() const override { return m_text; }
    void redo() override;
    void undo() override;
    [[nodiscard]] int mergeId() const override { return m_mergeId; }
    [[nodiscard]] bool mergeWith(const cmd::Command& next) override;

    [[nodiscard]] const SequenceEdit& edit() const noexcept { return m_edit; }

private:
    void notify(bool tracksToo);

    Document& m_doc;
    std::string m_text;
    SequenceEdit m_edit;
    int m_mergeId;
    bool m_captured = false;
    std::vector<std::pair<std::size_t, Clip>> m_removedBefore;   // original index + clip
    std::vector<Clip> m_modifiedBefore;
    std::vector<Track> m_tracksBefore;   // the whole track list (tracks are few)
    SequenceMarks m_marksBefore;
};

}  // namespace lf::project
