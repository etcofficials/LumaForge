#include "TestMain.h"

#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"

using namespace lf;
using namespace lf::project;

namespace {

constexpr Ticks kSec = kFlicksPerSecond;
inline TimePoint sec(double s) { return TimePoint::fromSeconds(s); }
inline Duration dsec(double s) { return Duration::fromSeconds(s); }

MediaId addVideo(Document& doc, const char* name, double seconds, bool audio = true) {
    MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = std::string("C:/media/") + name;
    m.displayName = name;
    m.info.path = m.path;
    m.info.type = media::MediaType::Video;
    m.info.duration = dsec(seconds);
    media::VideoStreamInfo v;
    v.width = 1280; v.height = 720; v.frameRate = fps::f30;
    m.info.video = v;
    if (audio) { media::AudioStreamInfo a; a.channels = 2; a.sampleRate = 48000; m.info.audio = a; }
    doc.push(std::make_unique<ImportMediaCommand>(doc, std::vector<MediaItem>{m}));
    return m.id;
}

// Adds the media at `at` on V1/A1 and returns the visual clip id.
ClipId place(Document& doc, MediaId media, double at) {
    const MediaItem* item = doc.project().findMedia(media);
    auto clips = ops::clipsForMedia(doc, *item, ops::defaultPlacement(doc.project(), *item, sec(at)));
    const ClipId id = clips.front().id;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, std::move(clips))));
    return id;
}

TrackId V1(const Document& d) { return d.project().sequence.primaryTrack(TrackKind::Video)->id; }
TrackId A1(const Document& d) { return d.project().sequence.primaryTrack(TrackKind::Audio)->id; }

}  // namespace

TEST_CASE("default sequence has V2 V1 A1 A2 and ids past them") {
    Document doc;
    const auto& seq = doc.project().sequence;
    CHECK(seq.tracks.size() == 4);
    CHECK(seq.tracks[0].name == "V2");
    CHECK(seq.tracks[1].name == "V1");
    CHECK(seq.tracks[2].name == "A1");
    CHECK(seq.tracks[3].name == "A2");
    CHECK(raw(doc.allocateTrackId()) == 5);
    CHECK(seq.primaryTrack(TrackKind::Video)->name == "V1");
    CHECK(seq.videoTracksBottomUp().front()->name == "V1");
}

TEST_CASE("adding media creates linked video+audio clips, undo removes both") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10);
    const ClipId v = place(doc, m, 0);
    const auto& seq = doc.project().sequence;
    CHECK(seq.clips.size() == 2);
    const Clip* vc = seq.findClip(v);
    CHECK(vc && vc->kind == ClipKind::Video && vc->track == V1(doc));
    const Clip* ac = seq.findClip(vc->link);
    CHECK(ac && ac->kind == ClipKind::Audio && ac->track == A1(doc) && ac->link == v);
    CHECK(vc->duration == dsec(10));
    CHECK(seq.duration() == dsec(10));
    doc.undoStack().undo();
    CHECK(seq.clips.empty());
    doc.undoStack().redo();
    CHECK(seq.clips.size() == 2 && seq.findClip(v) != nullptr);
}

TEST_CASE("clips appended at sequence end when no position given") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 4);
    place(doc, m, -1);
    const ClipId second = place(doc, m, -1);
    CHECK(doc.project().sequence.findClip(second)->start == sec(4));
    CHECK(doc.project().sequence.duration() == dsec(8));
}

TEST_CASE("source time mapping honours sourceIn and rational speed") {
    Clip c;
    c.start = sec(10);
    c.duration = dsec(4);
    c.sourceIn = sec(2);
    c.speed = Rational{2, 1};
    CHECK(c.sourceTimeAt(sec(11)) == sec(4));      // 1 s in at 2x = 2 s of media
    CHECK(c.sourceDuration() == dsec(8));
    CHECK(c.timelineDurationFor(dsec(8)) == dsec(4));
    c.speed = Rational{1, 2};
    CHECK(c.sourceTimeAt(sec(12)) == sec(3));
}

TEST_CASE("overwrite: dropping onto the middle of a clip splits it into two linked-consistent pieces") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 10);
    const MediaId b = addVideo(doc, "b.mp4", 2);
    place(doc, a, 0);
    place(doc, b, 4);
    const auto& seq = doc.project().sequence;
    // V1: a[0,4) b[4,6) a'[6,10)  and the same on A1
    auto v1 = seq.clipsOnTrack(V1(doc));
    CHECK(v1.size() == 3);
    CHECK(v1[0]->start == sec(0) && v1[0]->end() == sec(4));
    CHECK(v1[1]->start == sec(4) && v1[1]->end() == sec(6));
    CHECK(v1[2]->start == sec(6) && v1[2]->end() == sec(10));
    CHECK(v1[2]->sourceIn == sec(6));            // right piece continues the media
    auto a1 = seq.clipsOnTrack(A1(doc));
    CHECK(a1.size() == 3);
    CHECK(v1[2]->link == a1[2]->id && a1[2]->link == v1[2]->id);   // right pieces re-linked
    CHECK(v1[0]->link == a1[0]->id);                                // left pieces keep their link
    doc.undoStack().undo();
    CHECK(seq.clips.size() == 2 && seq.findClip(v1[0]->id)->duration == dsec(10));
}

TEST_CASE("overwrite trims neighbours that only partially overlap") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 4, false);
    place(doc, a, 0);
    place(doc, a, 10);
    // Drop a 4 s clip at 2: left neighbour trimmed to [0,2); nothing at [6,10)
    place(doc, a, 2);
    const auto& seq = doc.project().sequence;
    auto v1 = seq.clipsOnTrack(V1(doc));
    CHECK(v1.size() == 3);
    CHECK(v1[0]->end() == sec(2));
    CHECK(v1[1]->start == sec(2) && v1[1]->end() == sec(6));
    // Drop at 8: right neighbour [10,14) trimmed to [12,14) with sourceIn 2
    place(doc, a, 8);
    v1 = seq.clipsOnTrack(V1(doc));
    CHECK(v1.size() == 4);
    CHECK(v1[3]->start == sec(12) && v1[3]->sourceIn == sec(2) && v1[3]->duration == dsec(2));
}

TEST_CASE("move clips: clamped at zero, overwrites destination, track change requires same kind") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 2, false);
    const ClipId c1 = place(doc, a, 0);
    const ClipId c2 = place(doc, a, 5);
    const auto& seq = doc.project().sequence;

    auto e = ops::moveClips(doc, {c2}, dsec(-10), 0);
    CHECK(e.has_value());
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Move", *e));
    CHECK(seq.findClip(c2)->start == sec(0));     // clamped
    CHECK(seq.findClip(c1) == nullptr);           // fully overwritten
    doc.undoStack().undo();
    CHECK(seq.findClip(c1) != nullptr && seq.findClip(c2)->start == sec(5));

    // V1 -> V2 is one row up (display order V2, V1, A1, A2)
    e = ops::moveClips(doc, {c2}, Duration::zero(), -1);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Move", *e));
    CHECK(seq.findClip(c2)->track == seq.tracks[0].id);
    // V2 -> A1 (two rows down) is refused: time move only
    e = ops::moveClips(doc, {c2}, dsec(1), +2);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Move", *e));
    CHECK(seq.findClip(c2)->track == seq.tracks[0].id);
    CHECK(seq.findClip(c2)->start == sec(6));
}

TEST_CASE("move merges consecutive drags into one undo step") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 2, false);
    const ClipId c = place(doc, a, 0);
    const std::size_t before = doc.undoStack().count();
    for (int i = 1; i <= 3; ++i) {
        auto e = ops::moveClips(doc, {c}, dsec(1), 0);
        doc.push(std::make_unique<SequenceEditCommand>(doc, "Move Clip", *e, 7));
    }
    CHECK(doc.undoStack().count() == before + 1);
    CHECK(doc.project().sequence.findClip(c)->start == sec(3));
    doc.undoStack().undo();
    CHECK(doc.project().sequence.findClip(c)->start == sec(0));
}

TEST_CASE("trim: end clamped to media, start keeps sourceIn consistent, linked partner follows") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 10);
    const ClipId v = place(doc, a, 0);
    const auto& seq = doc.project().sequence;
    const ClipId au = seq.findClip(v)->link;

    doc.push(std::make_unique<SequenceEditCommand>(doc, "Trim", ops::trimClip(doc, v, ops::Edge::End, sec(4))));
    CHECK(seq.findClip(v)->duration == dsec(4));
    CHECK(seq.findClip(au)->duration == dsec(4));
    // Cannot extend past the media
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Trim", ops::trimClip(doc, v, ops::Edge::End, sec(30))));
    CHECK(seq.findClip(v)->duration == dsec(10));
    // Trim the start in by 3 s: starts at 3, shows media from 3
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Trim", ops::trimClip(doc, v, ops::Edge::Start, sec(3))));
    CHECK(seq.findClip(v)->start == sec(3) && seq.findClip(v)->sourceIn == sec(3) && seq.findClip(v)->duration == dsec(7));
    // Cannot reveal before media start
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Trim", ops::trimClip(doc, v, ops::Edge::Start, sec(-5))));
    CHECK(seq.findClip(v)->start == sec(0) && seq.findClip(v)->sourceIn == sec(0));
    // Minimum one frame
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Trim", ops::trimClip(doc, v, ops::Edge::End, sec(0))));
    CHECK(seq.findClip(v)->duration == frameDuration(fps::f30));
}

TEST_CASE("split at playhead produces frame-exact pieces; nothing happens at edges") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 10);
    const ClipId v = place(doc, a, 0);
    const auto& seq = doc.project().sequence;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Split", ops::splitAt(doc, sec(3))));
    CHECK(seq.clips.size() == 4);
    const Clip* left = seq.findClip(v);
    CHECK(left->duration == dsec(3));
    const Clip* right = seq.clipAt(V1(doc), sec(5));
    CHECK(right && right->start == sec(3) && right->sourceIn == sec(3) && right->duration == dsec(7));
    CHECK(isValid(right->link) && seq.findClip(right->link)->kind == ClipKind::Audio);
    CHECK(ops::splitAt(doc, sec(0)).empty());
    CHECK(ops::splitAt(doc, sec(10)).empty());
    doc.undoStack().undo();
    CHECK(seq.clips.size() == 2 && seq.findClip(v)->duration == dsec(10));
}

TEST_CASE("ripple delete closes the gap on all tracks; plain delete leaves it") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 2);
    const ClipId c1 = place(doc, a, 0);
    const ClipId c2 = place(doc, a, 2);
    const ClipId c3 = place(doc, a, 4);
    const auto& seq = doc.project().sequence;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Delete", ops::removeClips(doc, ops::withLinked(seq, {c2}), false)));
    CHECK(seq.clips.size() == 4 && seq.findClip(c3)->start == sec(4));
    doc.undoStack().undo();
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Ripple Delete", ops::removeClips(doc, ops::withLinked(seq, {c2}), true)));
    CHECK(seq.clips.size() == 4);
    CHECK(seq.findClip(c3)->start == sec(2));
    CHECK(seq.findClip(seq.findClip(c3)->link)->start == sec(2));
    CHECK(seq.findClip(c1)->start == sec(0));
    CHECK(seq.duration() == dsec(4));
}

TEST_CASE("snapping picks the nearest edge inside the tolerance only") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 2, false);
    place(doc, a, 0);
    place(doc, a, 5);
    const auto targets = ops::snapTargets(doc.project().sequence, {}, sec(9));
    CHECK(ops::snap(sec(1.9), targets, dsec(0.2)).value() == sec(2));
    CHECK(ops::snap(sec(4.85), targets, dsec(0.2)).value() == sec(5));
    CHECK(!ops::snap(sec(3.5), targets, dsec(0.2)).has_value());
    CHECK(ops::snap(sec(8.9), targets, dsec(0.2)).value() == sec(9));   // playhead
}

TEST_CASE("locked tracks are never edited") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 4, false);
    const ClipId c = place(doc, a, 0);
    auto& seq = doc.project().sequence;
    seq.findTrack(V1(doc))->locked = true;
    CHECK(ops::splitAt(doc, sec(1)).empty());
    CHECK(ops::removeClips(doc, {c}, false).empty());
    CHECK(ops::trimClip(doc, c, ops::Edge::End, sec(1)).empty());
    CHECK(!ops::moveClips(doc, {c}, dsec(1), 0).has_value());
}

TEST_CASE("sequence program composes V1 under V2 and maps clip time to media time") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 10);
    const MediaId b = addVideo(doc, "b.mp4", 10);
    const ClipId va = place(doc, a, 0);
    // Put b on V2 from 2..6 with sourceIn 1
    const MediaItem* itemB = doc.project().findMedia(b);
    ops::Placement p;
    p.videoTrack = doc.project().sequence.tracks[0].id;
    p.start = sec(2);
    auto clips = ops::clipsForMedia(doc, *itemB, p);
    clips[0].duration = dsec(4);
    clips[0].sourceIn = sec(1);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, clips)));

    playback::SequenceProgram prog(doc.project());
    CHECK(prog.duration() == dsec(10));
    CHECK(prog.frameWidth() == 1920 && prog.frameHeight() == 1080);
    std::vector<playback::VideoLayerRequest> layers;
    prog.videoLayersAt(sec(3), layers);
    CHECK(layers.size() == 2);
    CHECK(layers[0].path == "C:/media/a.mp4" && layers[0].sourceTime == sec(3));
    CHECK(layers[1].path == "C:/media/b.mp4" && layers[1].sourceTime == sec(2));
    CHECK(layers[0].layerKey == raw(va));
    layers.clear();
    prog.videoLayersAt(sec(8), layers);
    CHECK(layers.size() == 1);
    std::vector<playback::AudioClipRequest> audio;
    prog.audioClipsAt(sec(3), audio);
    CHECK(audio.size() == 1 && audio[0].gain == 1.0f);
    // Muting a track removes it from the program
    doc.project().sequence.tracks[0].muted = true;
    playback::SequenceProgram muted(doc.project());
    layers.clear();
    muted.videoLayersAt(sec(3), layers);
    CHECK(layers.size() == 1);
}

TEST_CASE("audio gain applies fades and mute") {
    Clip c;
    c.start = sec(0);
    c.duration = dsec(10);
    c.audio.volume = 0.5f;
    c.audio.fadeIn = dsec(2);
    c.audio.fadeOut = dsec(2);
    CHECK(std::abs(playback::SequenceProgram::gainAt(c, sec(1)) - 0.25f) < 1e-6f);
    CHECK(std::abs(playback::SequenceProgram::gainAt(c, sec(5)) - 0.5f) < 1e-6f);
    CHECK(std::abs(playback::SequenceProgram::gainAt(c, sec(9)) - 0.25f) < 1e-6f);
    c.audio.muted = true;
    CHECK(playback::SequenceProgram::gainAt(c, sec(5)) == 0.0f);
}

TEST_CASE("duplicate places copies right after the group, links remapped, properties kept") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 4);
    const ClipId v = place(doc, a, 1);
    auto& seq = doc.project().sequence;
    seq.findClip(v)->transform.scale = 0.5f;
    seq.findClip(v)->color.saturation = 1.5f;
    const ClipId au = seq.findClip(v)->link;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Duplicate", ops::duplicateClips(doc, ops::withLinked(seq, {v}))));
    CHECK(seq.clips.size() == 4);
    const Clip* copy = seq.clipAt(V1(doc), sec(6));
    CHECK(copy && copy->start == sec(5) && copy->duration == dsec(4));
    CHECK(copy->id != v && copy->transform.scale == 0.5f && copy->color.saturation == 1.5f && copy->media == a);
    const Clip* copyAudio = seq.findClip(copy->link);
    CHECK(copyAudio && copyAudio->kind == ClipKind::Audio && copyAudio->link == copy->id && copyAudio->id != au);
    doc.undoStack().undo();
    CHECK(seq.clips.size() == 2);
}

TEST_CASE("duplicate inserts: later clips shift right, a straddling clip is split around the copy") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 2, false);
    const MediaId b = addVideo(doc, "b.mp4", 10, false);
    const ClipId c1 = place(doc, a, 0);
    const ClipId c2 = place(doc, b, 2);       // [2,12)
    auto& seq = doc.project().sequence;
    // Duplicate c2's first half after splitting it at 5: pieces [2,5) and [5,12)
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Split", ops::splitClips(doc, {c2}, sec(5))));
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Duplicate", ops::duplicateClips(doc, {c2})));
    auto v1 = seq.clipsOnTrack(V1(doc));
    CHECK(v1.size() == 4);
    CHECK(v1[0]->id == c1);
    CHECK(v1[1]->id == c2 && v1[1]->end() == sec(5));
    CHECK(v1[2]->start == sec(5) && v1[2]->duration == dsec(3) && v1[2]->sourceIn == sec(0));   // the copy
    CHECK(v1[3]->start == sec(8) && v1[3]->duration == dsec(7) && v1[3]->sourceIn == sec(3));   // shifted, untouched
    // Duplicate c1 (ends at 2): everything from 2 on shifts by 2 s; nothing is lost
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Duplicate", ops::duplicateClips(doc, {c1})));
    v1 = seq.clipsOnTrack(V1(doc));
    CHECK(v1.size() == 5 && v1[1]->start == sec(2) && v1[2]->start == sec(4) && seq.duration() == dsec(17));
}

TEST_CASE("paste lands the earliest copy at the playhead and overwrites underneath") {
    Document doc;
    const MediaId a = addVideo(doc, "a.mp4", 2, false);
    const ClipId c1 = place(doc, a, 0);
    place(doc, a, 10);
    auto& seq = doc.project().sequence;
    std::vector<Clip> clipboard{*seq.findClip(c1)};
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Paste", ops::pasteClips(doc, clipboard, sec(11))));
    auto v1 = seq.clipsOnTrack(V1(doc));
    CHECK(v1.size() == 3);
    CHECK(v1[1]->start == sec(10) && v1[1]->duration == dsec(1));   // trimmed by the paste
    CHECK(v1[2]->start == sec(11) && v1[2]->id != c1);
    // Clipboard survives deletion of the original (it is a copy).
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Delete", ops::removeClips(doc, {c1}, false)));
    CHECK(!ops::pasteClips(doc, clipboard, sec(20)).empty());
    // Media removed from the project -> nothing to paste
    Project p = doc.project();
    p.media.clear();
    doc.reset(p);
    CHECK(ops::pasteClips(doc, clipboard, sec(0)).empty());
}
