// V0.4 editing operations: ripple trim, roll edit, lift/extract, In/Out marks,
// clip enable, edit-point navigation, and undo/redo regressions over all of them.
#include "TestMain.h"

#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"

using namespace lf;
using namespace lf::project;

namespace {

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

// Places `length` seconds of the media (starting at media time `in`) at `at` on V1/A1.
ClipId place(Document& doc, MediaId media, double at, double in = 0, double length = -1) {
    const MediaItem* item = doc.project().findMedia(media);
    auto clips = ops::clipsForMedia(doc, *item, ops::defaultPlacement(doc.project(), *item, sec(at)));
    for (auto& c : clips) {
        c.sourceIn = sec(in);
        if (length > 0) c.duration = dsec(length);
        else c.duration = item->info.duration - dsec(in);
    }
    const ClipId id = clips.front().id;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, std::move(clips))));
    return id;
}

void apply(Document& doc, const char* text, SequenceEdit e) { doc.push(std::make_unique<SequenceEditCommand>(doc, text, std::move(e))); }

}  // namespace

TEST_CASE("ripple trim end: shortening pulls later clips on every unlocked track left; undo/redo exact") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10);
    const ClipId a = place(doc, m, 0, 0, 4);
    const ClipId b = place(doc, m, 4, 4, 3);
    const auto& seq = doc.project().sequence;
    const auto before = seq.clips;
    apply(doc, "Ripple", ops::rippleTrim(doc, a, ops::Edge::End, sec(3)));
    CHECK(seq.findClip(a)->duration == dsec(3));
    CHECK(seq.findClip(seq.findClip(a)->link)->duration == dsec(3));   // linked audio trimmed with it
    CHECK(seq.findClip(b)->start == sec(3));
    CHECK(seq.findClip(seq.findClip(b)->link)->start == sec(3));       // audio of b shifted too
    CHECK(seq.duration() == dsec(6));
    doc.undoStack().undo();
    CHECK(seq.clips == before);
    doc.undoStack().redo();
    CHECK(seq.findClip(b)->start == sec(3));
}

TEST_CASE("ripple trim end: extending pushes later clips right; start ripple keeps the clip start") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10, false);
    const ClipId a = place(doc, m, 0, 0, 4);
    const ClipId b = place(doc, m, 4, 0, 2);
    const auto& seq = doc.project().sequence;
    apply(doc, "Ripple", ops::rippleTrim(doc, a, ops::Edge::End, sec(5)));
    CHECK(seq.findClip(a)->duration == dsec(5));
    CHECK(seq.findClip(b)->start == sec(5));
    // Head ripple: drag start to 1 s -> clip stays at 0, loses its first second, b follows.
    apply(doc, "Ripple", ops::rippleTrim(doc, a, ops::Edge::Start, sec(1)));
    CHECK(seq.findClip(a)->start == sec(0));
    CHECK(seq.findClip(a)->sourceIn == sec(1));
    CHECK(seq.findClip(a)->duration == dsec(4));
    CHECK(seq.findClip(b)->start == sec(4));
}

TEST_CASE("ripple shortening never pulls a clip on another track into its neighbour") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10, false);
    const ClipId a = place(doc, m, 0, 0, 4);
    // Upper track: one clip until 3.5 s and one starting at 4 s (only 0.5 s of slack).
    const auto& seq = doc.project().sequence;
    const TrackId v2 = seq.tracks[0].id;
    std::vector<Clip> upper;
    for (double start : {0.0, 4.0}) {
        Clip c = *seq.findClip(a);
        c.id = doc.allocateClipId();
        c.track = v2;
        c.start = sec(start);
        c.duration = start == 0.0 ? dsec(3.5) : dsec(2);
        upper.push_back(c);
    }
    const ClipId upperLate = upper[1].id;
    apply(doc, "Add", ops::insertClips(doc, upper));
    apply(doc, "Ripple", ops::rippleTrim(doc, a, ops::Edge::End, sec(2)));
    CHECK(seq.findClip(a)->duration == dsec(3.5));     // limited to the 0.5 s of slack
    CHECK(seq.findClip(upperLate)->start == sec(3.5));
}

TEST_CASE("roll edit moves the cut, keeps total length, respects media handles, rolls linked audio") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10);
    const ClipId a = place(doc, m, 0, 0, 4);   // media 0-4
    const ClipId b = place(doc, m, 4, 6, 3);   // media 6-9 (1 s handle after, 6 s before)
    const auto& seq = doc.project().sequence;
    apply(doc, "Roll", ops::rollEdit(doc, a, b, sec(5)));
    CHECK(seq.findClip(a)->duration == dsec(5));
    CHECK(seq.findClip(b)->start == sec(5));
    CHECK(seq.findClip(b)->sourceIn == sec(7));
    CHECK(seq.findClip(b)->duration == dsec(2));
    CHECK(seq.duration() == dsec(7));
    const Clip* aa = seq.findClip(seq.findClip(a)->link);
    const Clip* ba = seq.findClip(seq.findClip(b)->link);
    CHECK(aa->duration == dsec(5) && ba->start == sec(5));
    // Left clip cannot use more media than exists (b keeps at least a frame too).
    const auto cut = ops::cutAt(seq, seq.findClip(a)->track, sec(5.01), dsec(0.1));
    CHECK(cut.has_value() && cut->first == a && cut->second == b);
    apply(doc, "Roll", ops::rollEdit(doc, a, b, sec(0.01)));
    CHECK(seq.findClip(a)->duration >= frameDuration(fps::f30));
}

TEST_CASE("lift leaves a gap, extract closes it and clears In/Out; both undo") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10);
    const ClipId a = place(doc, m, 0, 0, 6);
    const ClipId b = place(doc, m, 6, 0, 2);
    const auto& seq = doc.project().sequence;
    SequenceEdit marks;
    marks.marks = SequenceMarks{sec(2), sec(4)};
    apply(doc, "Marks", std::move(marks));
    CHECK(seq.hasInOut());
    apply(doc, "Lift", ops::liftRange(doc, {seq.markIn, seq.markOut - seq.markIn}));
    CHECK(seq.findClip(a)->duration == dsec(2));
    CHECK(seq.findClip(b)->start == sec(6));
    CHECK(seq.duration() == dsec(8));
    doc.undoStack().undo();
    SequenceEdit ex = ops::extractRange(doc, {seq.markIn, seq.markOut - seq.markIn});
    ex.marks = SequenceMarks{};
    apply(doc, "Extract", std::move(ex));
    CHECK(seq.duration() == dsec(6));
    CHECK(seq.findClip(b)->start == sec(4));
    CHECK(!seq.hasInOut());
    doc.undoStack().undo();
    CHECK(seq.hasInOut() && seq.markIn == sec(2) && seq.duration() == dsec(8));
}

TEST_CASE("edit points: previous/next over every clip edge") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10, false);
    place(doc, m, 0, 0, 2);
    place(doc, m, 5, 0, 1);
    const auto& seq = doc.project().sequence;
    CHECK(ops::nextEditPoint(seq, sec(0)).value() == sec(2));
    CHECK(ops::nextEditPoint(seq, sec(2)).value() == sec(5));
    CHECK(ops::previousEditPoint(seq, sec(5.5)).value() == sec(5));
    CHECK(!ops::nextEditPoint(seq, sec(6)).has_value());
    CHECK(!ops::previousEditPoint(seq, sec(0)).has_value());
}

TEST_CASE("disabled clips are kept on the timeline but not rendered or mixed") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 4);
    const ClipId a = place(doc, m, 0);
    auto& seq = doc.project().sequence;
    for (auto& c : seq.clips) c.enabled = false;
    playback::SequenceProgram program(doc.project());
    std::vector<playback::VideoLayerRequest> layers;
    std::vector<playback::AudioClipRequest> audio;
    program.videoLayersAt(sec(1), layers);
    program.audioClipsAt(sec(1), audio);
    CHECK(layers.empty() && audio.empty());
    CHECK(seq.findClip(a) != nullptr);
}

TEST_CASE("project v3 round-trips clip enable and In/Out; v2 files still load") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 4);
    const ClipId a = place(doc, m, 0);
    auto& p = doc.project();
    p.sequence.findClip(a)->enabled = false;
    p.sequence.markIn = sec(1);
    p.sequence.markOut = sec(3);
    const std::string json = serializer::toJson(p, "");
    auto loaded = serializer::fromJson(json, "");
    CHECK(static_cast<bool>(loaded));
    CHECK(loaded->fileVersion == 3);
    CHECK(!loaded->project.sequence.findClip(a)->enabled);
    CHECK(loaded->project.sequence.markIn == sec(1) && loaded->project.sequence.markOut == sec(3));
    // An older file has neither key: defaults apply.
    std::string v2 = json;
    v2.replace(v2.find("\"version\": 3"), 12, "\"version\": 2");
    auto old = serializer::fromJson(v2, "");
    CHECK(static_cast<bool>(old) && old->fileVersion == 2);
}

// ---------------------------------------------------------------- undo/redo regressions

TEST_CASE("redo regression: undo -> redo restores every kind of edit exactly") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10);
    const ClipId a = place(doc, m, 0, 0, 4);
    const ClipId b = place(doc, m, 4, 4, 3);
    auto& seq = doc.project().sequence;
    auto& undo = doc.undoStack();
    std::vector<std::vector<Clip>> states{seq.clips};
    apply(doc, "Move", *ops::moveClips(doc, ops::withLinked(seq, {b}), dsec(1), 0)); states.push_back(seq.clips);
    apply(doc, "Trim", ops::trimClip(doc, a, ops::Edge::End, sec(3))); states.push_back(seq.clips);
    apply(doc, "Split", ops::splitAt(doc, sec(1))); states.push_back(seq.clips);
    apply(doc, "Ripple Delete", ops::removeClips(doc, ops::withLinked(seq, {a}), true)); states.push_back(seq.clips);
    {
        SequenceEdit e;
        Clip c = *seq.findClip(b);
        c.color.saturation = 0.2f;
        c.effects.push_back(makeEffect(EffectType::Sepia));
        c.speed = Rational{2, 1};
        e.modify(c);
        apply(doc, "Inspector", std::move(e));
        states.push_back(seq.clips);
    }
    {
        SequenceEdit e;
        Track t = seq.tracks[2];
        t.muted = true;
        e.tracks.push_back(t);
        apply(doc, "Mute Track", std::move(e));
        states.push_back(seq.clips);
    }
    // Walk all the way back and forward twice: every step must reproduce its state.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = static_cast<int>(states.size()) - 1; i > 0; --i) {
            undo.undo();
            CHECK(seq.clips == states[static_cast<std::size_t>(i - 1)]);
        }
        for (std::size_t i = 1; i < states.size(); ++i) {
            CHECK(undo.canRedo());
            undo.redo();
            CHECK(seq.clips == states[i]);
        }
    }
    CHECK(seq.tracks[2].muted);
    CHECK(!undo.canRedo());
}

TEST_CASE("redo regression: a new edit after undo clears the redo history") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10);
    const ClipId a = place(doc, m, 0, 0, 4);
    auto& undo = doc.undoStack();
    apply(doc, "Trim", ops::trimClip(doc, a, ops::Edge::End, sec(3)));
    undo.undo();
    CHECK(undo.canRedo());
    apply(doc, "Trim", ops::trimClip(doc, a, ops::Edge::End, sec(2)));
    CHECK(!undo.canRedo());
    CHECK(undo.redoText().empty());
    CHECK(doc.project().sequence.findClip(a)->duration == dsec(2));
}

TEST_CASE("redo regression: undo/redo across a save keeps the dirty flag truthful") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10);
    const ClipId a = place(doc, m, 0, 0, 4);
    auto& undo = doc.undoStack();
    apply(doc, "Trim", ops::trimClip(doc, a, ops::Edge::End, sec(3)));
    doc.markSaved();
    CHECK(!doc.isDirty());
    undo.undo();
    CHECK(doc.isDirty());
    undo.redo();
    CHECK(!doc.isDirty());
    // An edit merging into the saved command must not hide that the document changed.
    SequenceEdit e1;
    Clip c = *doc.project().sequence.findClip(a);
    c.transform.opacity = 0.5f;
    e1.modify(c);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Opacity", std::move(e1), 107));
    CHECK(doc.isDirty());
    undo.undo();
    CHECK(!doc.isDirty());
    undo.redo();
    CHECK(doc.isDirty() && doc.project().sequence.findClip(a)->transform.opacity == 0.5f);
}

TEST_CASE("redo regression: merged inspector drags undo and redo as one step with the right labels") {
    Document doc;
    const MediaId m = addVideo(doc, "a.mp4", 10);
    const ClipId a = place(doc, m, 0, 0, 4);
    auto& undo = doc.undoStack();
    const std::size_t base = undo.count();
    for (float v : {0.9f, 0.7f, 0.4f}) {
        SequenceEdit e;
        Clip c = *doc.project().sequence.findClip(a);
        c.transform.opacity = v;
        e.modify(c);
        doc.push(std::make_unique<SequenceEditCommand>(doc, "Opacity", std::move(e), 4242));
    }
    CHECK(undo.count() == base + 1);
    CHECK(undo.undoText() == "Opacity");
    undo.undo();
    CHECK(doc.project().sequence.findClip(a)->transform.opacity == 1.0f);
    CHECK(undo.redoText() == "Opacity");
    undo.redo();
    CHECK(doc.project().sequence.findClip(a)->transform.opacity == 0.4f);
}

TEST_CASE("In/Out marks are undoable edits that never merge") {
    Document doc;
    auto& seq = doc.project().sequence;
    SequenceEdit in;
    in.marks = SequenceMarks{sec(1), TimePoint{-1}};
    apply(doc, "Mark In", std::move(in));
    SequenceEdit out;
    out.marks = SequenceMarks{sec(1), sec(5)};
    apply(doc, "Mark Out", std::move(out));
    CHECK(seq.hasInOut());
    doc.undoStack().undo();
    CHECK(seq.markIn == sec(1) && seq.markOut.ticks == -1);
    doc.undoStack().undo();
    CHECK(seq.markIn.ticks == -1);
    doc.undoStack().redo();
    doc.undoStack().redo();
    CHECK(seq.markOut == sec(5));
}
