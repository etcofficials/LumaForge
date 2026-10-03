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
        m_doc.notify(Change{Change::Kind::MediaOrganized, {m_id}, {}});
    }
}

void RenameMediaCommand::undo() {
    if (auto* m = m_doc.project().findMedia(m_id)) {
        m->displayName = m_oldName;
        m_doc.notify(Change{Change::Kind::MediaOrganized, {m_id}, {}});
    }
}

// ---------------------------------------------------------------- Bins

OrganizeMediaCommand::OrganizeMediaCommand(Document& doc, std::string text, std::vector<std::string> bins, std::vector<std::pair<MediaId, std::string>> assign)
    : m_doc(doc), m_text(std::move(text)), m_bins(std::move(bins)), m_assign(std::move(assign)) {}

void OrganizeMediaCommand::apply(const std::vector<std::string>& bins, const std::vector<std::pair<MediaId, std::string>>& assign) {
    Project& p = m_doc.project();
    p.bins = bins;
    Change change{Change::Kind::MediaOrganized, {}, {}};
    for (const auto& [id, bin] : assign) {
        if (auto* m = p.findMedia(id)) { m->bin = bin; change.mediaIds.push_back(id); }
    }
    m_doc.notify(change);
}

void OrganizeMediaCommand::redo() {
    const Project& p = m_doc.project();
    m_oldBins = p.bins;
    m_oldAssign.clear();
    for (const auto& [id, bin] : m_assign) if (const auto* m = p.findMedia(id)) m_oldAssign.emplace_back(id, m->bin);
    apply(m_bins, m_assign);
}

void OrganizeMediaCommand::undo() { apply(m_oldBins, m_oldAssign); }

std::string OrganizeMediaCommand::uniqueBinName(const Project& project, const std::string& base) {
    auto taken = [&](const std::string& n) { return std::find(project.bins.begin(), project.bins.end(), n) != project.bins.end(); };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        std::string n = base + " " + std::to_string(i);
        if (!taken(n)) return n;
    }
}

cmd::CommandPtr OrganizeMediaCommand::createBin(Document& doc, const std::string& name) {
    const Project& p = doc.project();
    if (name.empty() || std::find(p.bins.begin(), p.bins.end(), name) != p.bins.end()) return nullptr;
    auto bins = p.bins;
    bins.push_back(name);
    return std::make_unique<OrganizeMediaCommand>(doc, "New Bin", std::move(bins), std::vector<std::pair<MediaId, std::string>>{});
}

cmd::CommandPtr OrganizeMediaCommand::renameBin(Document& doc, const std::string& from, const std::string& to) {
    const Project& p = doc.project();
    auto it = std::find(p.bins.begin(), p.bins.end(), from);
    if (it == p.bins.end() || to.empty() || from == to || std::find(p.bins.begin(), p.bins.end(), to) != p.bins.end()) return nullptr;
    auto bins = p.bins;
    bins[static_cast<std::size_t>(it - p.bins.begin())] = to;
    std::vector<std::pair<MediaId, std::string>> assign;
    for (const auto& m : p.media) if (m.bin == from) assign.emplace_back(m.id, to);
    return std::make_unique<OrganizeMediaCommand>(doc, "Rename Bin", std::move(bins), std::move(assign));
}

cmd::CommandPtr OrganizeMediaCommand::deleteBin(Document& doc, const std::string& name) {
    const Project& p = doc.project();
    if (std::find(p.bins.begin(), p.bins.end(), name) == p.bins.end()) return nullptr;
    auto bins = p.bins;
    bins.erase(std::remove(bins.begin(), bins.end(), name), bins.end());
    std::vector<std::pair<MediaId, std::string>> assign;
    for (const auto& m : p.media) if (m.bin == name) assign.emplace_back(m.id, std::string());
    return std::make_unique<OrganizeMediaCommand>(doc, "Delete Bin", std::move(bins), std::move(assign));
}

cmd::CommandPtr OrganizeMediaCommand::moveToBin(Document& doc, const std::vector<MediaId>& ids, const std::string& bin) {
    const Project& p = doc.project();
    auto bins = p.bins;
    if (!bin.empty() && std::find(bins.begin(), bins.end(), bin) == bins.end()) bins.push_back(bin);   // moving into a new bin creates it
    std::vector<std::pair<MediaId, std::string>> assign;
    for (auto id : ids) if (const auto* m = p.findMedia(id); m && m->bin != bin) assign.emplace_back(id, bin);
    if (assign.empty() && bins == p.bins) return nullptr;
    return std::make_unique<OrganizeMediaCommand>(doc, bin.empty() ? "Remove from Bin" : "Move to Bin", std::move(bins), std::move(assign));
}

// ---------------------------------------------------------------- Relink

RelinkMediaCommand::RelinkMediaCommand(Document& doc, MediaId id, MediaItem relinked) : m_doc(doc), m_id(id), m_new(std::move(relinked)) {
    m_new.id = id;   // identity never changes: clips keep pointing at it
}

void RelinkMediaCommand::redo() {
    if (auto* m = m_doc.project().findMedia(m_id)) {
        m_old = *m;
        *m = m_new;
        m_doc.notify(Change{Change::Kind::MediaChanged, {m_id}, {}});
    }
}

void RelinkMediaCommand::undo() {
    if (auto* m = m_doc.project().findMedia(m_id)) {
        *m = m_old;
        m_doc.notify(Change{Change::Kind::MediaChanged, {m_id}, {}});
    }
}

// ---------------------------------------------------------------- Settings

ProjectSettingsCommand::ProjectSettingsCommand(Document& doc, ProjectSettings settings) : m_doc(doc), m_new(std::move(settings)) {}

void ProjectSettingsCommand::redo() {
    m_old = m_doc.project().settings;
    m_doc.project().settings = m_new;
    m_doc.notify(Change{Change::Kind::Settings, {}, {}});
}

void ProjectSettingsCommand::undo() {
    m_doc.project().settings = m_old;
    m_doc.notify(Change{Change::Kind::Settings, {}, {}});
}

}  // namespace lf::project
