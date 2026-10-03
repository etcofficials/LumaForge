// V0.4 phase 5: media bins, effect library, title presets.
#include "TestMain.h"

#include "lumaforge/core/VisualParams.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"
#include "lumaforge/project/TitlePresets.h"

#include <set>

using namespace lf;
using namespace lf::project;

namespace {

MediaId addMedia(Document& doc, const char* name, media::MediaType type = media::MediaType::Video) {
    MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = std::string("C:/media/") + name;
    m.displayName = name;
    m.info.path = m.path;
    m.info.type = type;
    m.info.duration = Duration::fromSeconds(4);
    if (type != media::MediaType::Audio) {
        media::VideoStreamInfo v;
        v.width = 1280; v.height = 720; v.frameRate = fps::f30;
        m.info.video = v;
    }
    if (type != media::MediaType::Image) { media::AudioStreamInfo a; a.channels = 2; a.sampleRate = 48000; m.info.audio = a; }
    doc.push(std::make_unique<ImportMediaCommand>(doc, std::vector<MediaItem>{m}));
    return m.id;
}

void push(Document& doc, cmd::CommandPtr c) {
    CHECK(c != nullptr);
    if (c) doc.push(std::move(c));
}

}  // namespace

TEST_CASE("bins: create, move, rename, delete are single undo steps and redo exactly") {
    Document doc;
    const MediaId a = addMedia(doc, "a.mp4");
    const MediaId b = addMedia(doc, "b.wav", media::MediaType::Audio);
    const auto& p = doc.project();
    int organized = 0;
    doc.addListener([&](const Change& c) { if (c.kind == Change::Kind::MediaOrganized) ++organized; });

    push(doc, OrganizeMediaCommand::createBin(doc, "B-roll"));
    CHECK(p.bins == std::vector<std::string>{"B-roll"});
    CHECK(OrganizeMediaCommand::createBin(doc, "B-roll") == nullptr);   // duplicate names refused
    CHECK(OrganizeMediaCommand::uniqueBinName(p, "B-roll") == "B-roll 2");

    push(doc, OrganizeMediaCommand::moveToBin(doc, {a, b}, "B-roll"));
    CHECK(p.findMedia(a)->bin == "B-roll" && p.findMedia(b)->bin == "B-roll");
    CHECK(OrganizeMediaCommand::moveToBin(doc, {a}, "B-roll") == nullptr);   // nothing changes

    push(doc, OrganizeMediaCommand::renameBin(doc, "B-roll", "Cutaways"));
    CHECK(p.bins == std::vector<std::string>{"Cutaways"});
    CHECK(p.findMedia(a)->bin == "Cutaways" && p.findMedia(b)->bin == "Cutaways");

    // Moving into a bin that does not exist yet creates it.
    push(doc, OrganizeMediaCommand::moveToBin(doc, {b}, "Music"));
    CHECK(p.bins == (std::vector<std::string>{"Cutaways", "Music"}));

    push(doc, OrganizeMediaCommand::deleteBin(doc, "Cutaways"));
    CHECK(p.bins == std::vector<std::string>{"Music"});
    CHECK(p.findMedia(a)->bin.empty());          // items survive, unsorted
    CHECK(p.findMedia(b)->bin == "Music");
    CHECK(organized == 5);

    // Undo all five organise steps one by one, then redo them.
    for (int i = 0; i < 5; ++i) doc.undoStack().undo();
    CHECK(p.bins.empty());
    CHECK(p.findMedia(a)->bin.empty() && p.findMedia(b)->bin.empty());
    doc.undoStack().undo();   // second import
    CHECK(p.media.size() == 1);
    doc.undoStack().redo();
    for (int i = 0; i < 5; ++i) doc.undoStack().redo();
    CHECK(p.bins == std::vector<std::string>{"Music"});
    CHECK(p.findMedia(a)->bin.empty() && p.findMedia(b)->bin == "Music");
}

TEST_CASE("rename media notifies MediaOrganized (no thumbnail invalidation) and undoes") {
    Document doc;
    const MediaId a = addMedia(doc, "a.mp4");
    std::vector<Change::Kind> kinds;
    doc.addListener([&](const Change& c) { kinds.push_back(c.kind); });
    doc.push(std::make_unique<RenameMediaCommand>(doc, a, "Interview"));
    CHECK(doc.project().findMedia(a)->displayName == "Interview");
    CHECK(kinds.size() == 1 && kinds.back() == Change::Kind::MediaOrganized);
    doc.undoStack().undo();
    CHECK(doc.project().findMedia(a)->displayName == "a.mp4");
}

TEST_CASE("serializer v4: bins and bin assignments round-trip; older files load without them") {
    Document doc;
    const MediaId a = addMedia(doc, "a.mp4");
    addMedia(doc, "b.mp4");
    push(doc, OrganizeMediaCommand::moveToBin(doc, {a}, "Selects"));
    push(doc, OrganizeMediaCommand::createBin(doc, "Empty bin"));
    const std::string json = serializer::toJson(doc.project(), "");
    auto loaded = serializer::fromJson(json, "");
    CHECK(static_cast<bool>(loaded));
    if (!loaded) return;
    CHECK(loaded->fileVersion == 4);
    CHECK(loaded->project.bins == (std::vector<std::string>{"Selects", "Empty bin"}));
    CHECK(loaded->project.findMedia(a)->bin == "Selects");
    CHECK(loaded->project.media[1].bin.empty());

    std::string v3 = json;
    v3.replace(v3.find("\"version\": 4"), 12, "\"version\": 3");
    const auto binsKey = v3.find("\"bins\"");
    CHECK(binsKey != std::string::npos);
    auto old = serializer::fromJson(v3, "");
    CHECK(static_cast<bool>(old) && old->fileVersion == 3);
}

TEST_CASE("effects: new types fold into colour and looks; stacking rules") {
    CHECK(effectTypeFromId("pixelate") == EffectType::Pixelate);
    CHECK(effectTypeFromId("hueShift") == EffectType::HueShift);
    CHECK(effectTypeFromId("nope") == EffectType::Count);
    std::set<std::string> ids;
    for (int i = 0; i < static_cast<int>(EffectType::Count); ++i) {
        const auto& info = effectInfo(static_cast<EffectType>(i));
        ids.insert(info.id);
        CHECK(info.min < info.max && info.def >= info.min && info.def <= info.max);
        CHECK(info.description && *info.description);
    }
    CHECK(ids.size() == static_cast<std::size_t>(EffectType::Count));   // ids unique

    std::vector<Effect> fx;
    Effect e = makeEffect(EffectType::Exposure); e.amount = 1.0f; fx.push_back(e);
    e = makeEffect(EffectType::HueShift); e.amount = 45.0f; fx.push_back(e);
    e = makeEffect(EffectType::Temperature); e.amount = 0.5f; fx.push_back(e);
    e = makeEffect(EffectType::Posterize); e.amount = 8.0f; fx.push_back(e);
    e = makeEffect(EffectType::Posterize); e.amount = 4.0f; fx.push_back(e);   // coarser wins
    e = makeEffect(EffectType::Pixelate); e.amount = 10.0f; fx.push_back(e);
    e = makeEffect(EffectType::Pixelate); e.amount = 30.0f; fx.push_back(e);   // larger wins
    e = makeEffect(EffectType::Invert); e.amount = 0.5f; e.enabled = false; fx.push_back(e);   // disabled: ignored
    ColorAdjust color;
    EffectParams looks;
    applyEffects(fx, color, looks);
    CHECK(color.exposure == 1.0f && color.hue == 45.0f && color.temperature == 0.5f);
    CHECK(looks.posterize == 4.0f);
    CHECK(looks.pixelate == 30.0f);
    CHECK(looks.invert == 0.0f);
    CHECK(!looks.isIdentity());
}

TEST_CASE("addEffect: visual clips on unlocked tracks only; one undo step") {
    Document doc;
    const MediaId m = addMedia(doc, "a.mp4");
    const MediaItem* item = doc.project().findMedia(m);
    auto clips = ops::clipsForMedia(doc, *item, ops::defaultPlacement(doc.project(), *item, TimePoint::zero()));
    CHECK(clips.size() == 2);   // video + linked audio
    std::vector<ClipId> ids;
    for (const auto& c : clips) ids.push_back(c.id);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, std::move(clips))));
    const auto& seq = doc.project().sequence;

    SequenceEdit edit = ops::addEffect(seq, ids, makeEffect(EffectType::Blur));
    CHECK(edit.modified.size() == 1);   // the audio half is skipped
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add Blur", std::move(edit)));
    const Clip* video = nullptr;
    for (auto id : ids) if (const Clip* c = seq.findClip(id); c && c->kind != ClipKind::Audio) video = c;
    CHECK(video && video->effects.size() == 1 && video->effects[0].type == EffectType::Blur);
    doc.undoStack().undo();
    CHECK(seq.findClip(video->id)->effects.empty());
    doc.undoStack().redo();
    CHECK(seq.findClip(video->id)->effects.size() == 1);

    // Locked track: nothing applies.
    for (auto& t : doc.project().sequence.tracks) t.locked = true;
    CHECK(ops::addEffect(seq, ids, makeEffect(EffectType::Sepia)).modified.empty());
}

TEST_CASE("title presets: every preset makes a text clip that fits the frame") {
    const auto presets = titlePresets();
    CHECK(presets.size() >= 7);
    std::set<std::string> ids;
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        ids.insert(presets[static_cast<std::size_t>(i)].id);
        const Clip c = makeTitleClip(i, 1920, 1080);
        CHECK(c.kind == ClipKind::Text);
        CHECK(c.duration == ops::kDefaultTextDuration);
        CHECK(!c.text.text.empty() && c.text.text == presets[static_cast<std::size_t>(i)].sampleText);
        CHECK(c.text.fontSize > 20.0f && c.text.fontSize < 200.0f);
        CHECK(std::abs(c.transform.positionX) < 960.0f && std::abs(c.transform.positionY) < 540.0f);
    }
    CHECK(ids.size() == presets.size());
    // Applying a preset keeps typed text, but replaces the placeholder.
    Clip c = makeTitleClip(0, 1920, 1080);
    c.text.text = "My Holiday";
    applyTitlePreset(c, 2, 1920, 1080);
    CHECK(c.text.text == "My Holiday" && c.text.align == TextAlign::Left && c.text.backgroundOpacity > 0.0f);
    c.text.text = "Title";
    applyTitlePreset(c, 5, 1920, 1080);
    CHECK(c.text.text == "Note");
    // Scales with the frame: a 720p callout is two thirds the size of the 1080p one.
    CHECK(std::abs(makeTitleClip(5, 1280, 720).text.fontSize - makeTitleClip(5, 1920, 1080).text.fontSize * 2.0f / 3.0f) < 0.01f);
}
