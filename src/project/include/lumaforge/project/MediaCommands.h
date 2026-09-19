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
