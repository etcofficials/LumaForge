#pragma once

#include "lumaforge/commands/Command.h"
#include "lumaforge/project/Document.h"

#include <vector>

namespace lf::project {

// Adds already-probed items to the bin. Ids must be allocated by the caller
// (Document::allocateMediaId) so redo re-inserts the same identities.
class ImportMediaCommand final : public cmd::Command {
public:
    ImportMediaCommand(Document& doc, std::vector<MediaItem> items);
    [[nodiscard]] std::string text() const override;
    void redo() override;
    void undo() override;

private:
    Document& m_doc;
    std::vector<MediaItem> m_items;
};

class RemoveMediaCommand final : public cmd::Command {
public:
    RemoveMediaCommand(Document& doc, std::vector<MediaId> ids);
    [[nodiscard]] std::string text() const override;
    void redo() override;
    void undo() override;

private:
    Document& m_doc;
    std::vector<MediaId> m_ids;
    std::vector<std::pair<std::size_t, MediaItem>> m_removed;   // original index + item
};

// Points a (usually offline) media item at a different file, with its fresh probe.
class RelinkMediaCommand final : public cmd::Command {
public:
    RelinkMediaCommand(Document& doc, MediaId id, MediaItem relinked);
    [[nodiscard]] std::string text() const override { return "Relink Media"; }
    void redo() override;
    void undo() override;

private:
    Document& m_doc;
    MediaId m_id;
    MediaItem m_new;
    MediaItem m_old;
};

class ProjectSettingsCommand final : public cmd::Command {
public:
    ProjectSettingsCommand(Document& doc, ProjectSettings settings);
    [[nodiscard]] std::string text() const override { return "Project Settings"; }
    void redo() override;
    void undo() override;

private:
    Document& m_doc;
    ProjectSettings m_new;
    ProjectSettings m_old;
};

// Bin list and bin assignments, as one undoable step: create/rename/delete a
// bin, move items between bins. `assign` lists only the items that change.
class OrganizeMediaCommand final : public cmd::Command {
public:
    OrganizeMediaCommand(Document& doc, std::string text, std::vector<std::string> bins, std::vector<std::pair<MediaId, std::string>> assign);
    [[nodiscard]] std::string text() const override { return m_text; }
    void redo() override;
    void undo() override;

    // Ready-made edits (nullptr when there is nothing to do).
    static cmd::CommandPtr createBin(Document& doc, const std::string& name);
    static cmd::CommandPtr renameBin(Document& doc, const std::string& from, const std::string& to);
    static cmd::CommandPtr deleteBin(Document& doc, const std::string& name);   // items become unsorted
    static cmd::CommandPtr moveToBin(Document& doc, const std::vector<MediaId>& ids, const std::string& bin);
    // A name not yet used by a bin: "Bin", "Bin 2", ...
    [[nodiscard]] static std::string uniqueBinName(const Project& project, const std::string& base);

private:
    void apply(const std::vector<std::string>& bins, const std::vector<std::pair<MediaId, std::string>>& assign);

    Document& m_doc;
    std::string m_text;
    std::vector<std::string> m_bins, m_oldBins;
    std::vector<std::pair<MediaId, std::string>> m_assign, m_oldAssign;
};

class RenameMediaCommand final : public cmd::Command {
public:
    RenameMediaCommand(Document& doc, MediaId id, std::string newName);
    [[nodiscard]] std::string text() const override { return "Rename Media"; }
    void redo() override;
    void undo() override;

private:
    Document& m_doc;
    MediaId m_id;
    std::string m_newName;
    std::string m_oldName;
};

}  // namespace lf::project
