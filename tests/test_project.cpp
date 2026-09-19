#include "TestMain.h"

#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"

using namespace lf;
using namespace lf::project;

namespace {
MediaItem makeItem(Document& doc, const char* name) {
    MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = std::string("C:/media/") + name;
    m.displayName = name;
    m.info.type = media::MediaType::Video;
    return m;
}
}  // namespace

TEST_CASE("import command adds media and undo removes it, notifying listeners") {
    Document doc;
    int added = 0, removed = 0;
    doc.addListener([&](const Change& c) {
        if (c.kind == Change::Kind::MediaAdded) added += static_cast<int>(c.mediaIds.size());
        if (c.kind == Change::Kind::MediaRemoved) removed += static_cast<int>(c.mediaIds.size());
    });
    std::vector<MediaItem> items{makeItem(doc, "a.mp4"), makeItem(doc, "b.mp4")};
    const MediaId firstId = items[0].id;
    doc.push(std::make_unique<ImportMediaCommand>(doc, items));
    CHECK(doc.project().media.size() == 2);
    CHECK(added == 2);
    CHECK(doc.isDirty());
    doc.undoStack().undo();
    CHECK(doc.project().media.empty());
    CHECK(removed == 2);
    doc.undoStack().redo();
    CHECK(doc.project().media.size() == 2);
    CHECK(doc.project().findMedia(firstId) != nullptr);   // same identity after redo
}

TEST_CASE("remove command restores original order on undo") {
    Document doc;
    std::vector<MediaItem> items{makeItem(doc, "a"), makeItem(doc, "b"), makeItem(doc, "c")};
    doc.push(std::make_unique<ImportMediaCommand>(doc, items));
    doc.push(std::make_unique<RemoveMediaCommand>(doc, std::vector<MediaId>{items[1].id}));
    CHECK(doc.project().media.size() == 2);
    CHECK(doc.project().media[1].displayName == "c");
    doc.undoStack().undo();
    CHECK(doc.project().media.size() == 3);
    CHECK(doc.project().media[1].displayName == "b");
}

TEST_CASE("ids are never reused after reset/noteIdsInUse") {
    Document doc;
    Project p;
    MediaItem m;
    m.id = static_cast<MediaId>(41);
    p.media.push_back(m);
    doc.reset(std::move(p));
    CHECK(raw(doc.allocateMediaId()) == 42);
    CHECK(!doc.isDirty());
}

TEST_CASE("rename command round-trips") {
    Document doc;
    std::vector<MediaItem> items{makeItem(doc, "old")};
    const MediaId id = items[0].id;
    doc.push(std::make_unique<ImportMediaCommand>(doc, items));
    doc.push(std::make_unique<RenameMediaCommand>(doc, id, "new"));
    CHECK(doc.project().findMedia(id)->displayName == "new");
    doc.undoStack().undo();
    CHECK(doc.project().findMedia(id)->displayName == "old");
}
