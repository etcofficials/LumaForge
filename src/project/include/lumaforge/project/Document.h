#pragma once

#include "lumaforge/commands/UndoStack.h"
#include "lumaforge/project/Project.h"

#include <functional>
#include <string>
#include <vector>

namespace lf::project {

// What changed, so observers (UI, caches, playback) can react precisely
// instead of rebuilding everything on every edit.
struct Change {
    enum class Kind {
        Reset,            // whole project replaced (new/open)
        Settings,
        MediaAdded,
        MediaRemoved,
        MediaChanged,     // the file behind an item changed (relink): thumbnails etc. are stale
        MediaOrganized,   // names, bins or bin assignments changed; the files did not
        TracksChanged,    // track added/removed/reordered/muted/locked
        ClipsChanged,     // any clip geometry or property change
        MarksChanged,     // sequence In/Out points (and markers)
    };
    Kind kind;
    std::vector<MediaId> mediaIds;
    std::vector<ClipId> clipIds;
};

// Owns the Project, the undo history and the change-notification fan-out.
// All mutations happen on the UI thread through commands pushed here.
class Document {
public:
    using Listener = std::function<void(const Change&)>;

    Document();
    ~Document();

    [[nodiscard]] Project& project() noexcept { return m_project; }
    [[nodiscard]] const Project& project() const noexcept { return m_project; }
    [[nodiscard]] cmd::UndoStack& undoStack() noexcept { return m_undo; }

    // Executes and records a command. Every user edit goes through here.
    void push(cmd::CommandPtr command) { m_undo.push(std::move(command)); }

    int addListener(Listener listener);
    void removeListener(int token);
    void notify(const Change& change);

    // Identity allocation; ids are never reused within a document.
    MediaId allocateMediaId();
    ClipId allocateClipId();
    TrackId allocateTrackId();
    void noteIdsInUse();   // after load: bumps counters past every id in the project

    // File association
    [[nodiscard]] const std::string& filePath() const noexcept { return m_filePath; }
    void setFilePath(std::string path) { m_filePath = std::move(path); }
    [[nodiscard]] bool isDirty() const noexcept { return !m_undo.isClean() || m_modified; }
    void markSaved() { m_undo.setClean(); m_modified = false; }
    // Unsaved without an undo step (e.g. a project recovered from an autosave).
    void markModified() { m_modified = true; notify(Change{Change::Kind::Settings, {}, {}}); }

    // Replaces the whole project (new / open) and clears history.
    void reset(Project project);

private:
    Project m_project;
    cmd::UndoStack m_undo;
    std::vector<std::pair<int, Listener>> m_listeners;
    int m_nextToken = 1;
    std::uint64_t m_nextMediaId = 1;
    std::uint64_t m_nextClipId = 1;
    std::uint64_t m_nextTrackId = 1;
    std::string m_filePath;
    bool m_modified = false;
};

}  // namespace lf::project
