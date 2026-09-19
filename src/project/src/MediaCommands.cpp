#include "lumaforge/project/MediaCommands.h"

#include <algorithm>

namespace lf::project {

// ---------------------------------------------------------------- Import

ImportMediaCommand::ImportMediaCommand(Document& doc, std::vector<MediaItem> items) : m_doc(doc), m_items(std::move(items)) {}

std::string ImportMediaCommand::text() const {
    return m_items.size() == 1 ? "Import Media" : "Import " + std::to_string(m_items.size()) + " Media Files";
}

void ImportMediaCommand::redo() {
    auto& media = m_doc.project().media;
    Change change{Change::Kind::MediaAdded, {}, {}};
    for (const auto& item : m_items) {
        media.push_back(item);
        change.mediaIds.push_back(item.id);
    }
    m_doc.notify(change);
}

void ImportMediaCommand::undo() {
    auto& media = m_doc.project().media;
    Change change{Change::Kind::MediaRemoved, {}, {}};
    for (const auto& item : m_items) {
        std::erase_if(media, [&](const MediaItem& m) { return m.id == item.id; });
        change.mediaIds.push_back(item.id);
    }
    m_doc.notify(change);
}

// ---------------------------------------------------------------- Remove

RemoveMediaCommand::RemoveMediaCommand(Document& doc, std::vector<MediaId> ids) : m_doc(doc), m_ids(std::move(ids)) {}

std::string RemoveMediaCommand::text() const {
    return m_ids.size() == 1 ? "Remove Media" : "Remove " + std::to_string(m_ids.size()) + " Media Files";
}

void RemoveMediaCommand::redo() {
    auto& media = m_doc.project().media;
    m_removed.clear();
    Change change{Change::Kind::MediaRemoved, {}, {}};
    for (MediaId id : m_ids) {
        auto it = std::find_if(media.begin(), media.end(), [id](const MediaItem& m) { return m.id == id; });
        if (it == media.end()) continue;
        m_removed.emplace_back(static_cast<std::size_t>(it - media.begin()), *it);
        media.erase(it);
        change.mediaIds.push_back(id);
    }
    m_doc.notify(change);
}

void RemoveMediaCommand::undo() {
    auto& media = m_doc.project().media;
    Change change{Change::Kind::MediaAdded, {}, {}};
    // Re-insert in ascending original order so indices stay valid.
    std::sort(m_removed.begin(), m_removed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [index, item] : m_removed) {
        media.insert(media.begin() + static_cast<std::ptrdiff_t>(std::min(index, media.size())), item);
        change.mediaIds.push_back(item.id);
    }
    m_doc.notify(change);
}

// ---------------------------------------------------------------- Rename

RenameMediaCommand::RenameMediaCommand(Document& doc, MediaId id, std::string newName) : m_doc(doc), m_id(id), m_newName(std::move(newName)) {}

void RenameMediaCommand::redo() {
    if (auto* m = m_doc.project().findMedia(m_id)) {
        m_oldName = m->displayName;
        m->displayName = m_newName;
        m_doc.notify(Change{Change::Kind::MediaChanged, {m_id}, {}});
    }
}

void RenameMediaCommand::undo() {
    if (auto* m = m_doc.project().findMedia(m_id)) {
        m->displayName = m_oldName;
        m_doc.notify(Change{Change::Kind::MediaChanged, {m_id}, {}});
    }
}

}  // namespace lf::project
