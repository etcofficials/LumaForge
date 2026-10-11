// V0.4 phase 8: transitions — windows at cuts and free edges, placement ops,
// rendering through the shared program snapshot, splits, project files.
#include "TestMain.h"

#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"

#include <cmath>

using namespace lf;
using namespace lf::project;

namespace {

TimePoint sec(double s) { return TimePoint::fromSeconds(s); }
Duration dsec(double s) { return Duration::fromSeconds(s); }
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// Project with media "a.mp4" (20 s) and two clips on V1: A = media 0..4 at 0, B = media 10..14 at 4.
struct Fixture {
    Project p;
    ClipId a{1}, b{2};
    Fixture() {
        MediaItem m;
        m.id = MediaId{1};
        m.path = "G:/Caches/tmp/does-not-matter.mp4";
        m.info.type = media::MediaType::Video;
        m.info.duration = dsec(20);
        media::VideoStreamInfo v; v.width = 1920; v.height = 1080; v.frameRate = fps::f30; m.info.video = v;
        p.media.push_back(m);
        const TrackId v1 = p.sequence.primaryTrack(TrackKind::Video)->id;
        Clip ca;
        ca.id = a; ca.track = v1; ca.kind = ClipKind::Video; ca.media = m.id; ca.start = sec(0); ca.duration = dsec(4); ca.sourceIn = sec(0);
        Clip cb = ca;
        cb.id = b; cb.start = sec(4); cb.sourceIn = sec(10);
        p.sequence.clips = {ca, cb};
    }
    Clip& A() { return *p.sequence.findClip(a); }
    Clip& B() { return *p.sequence.findClip(b); }
    std::vector<TransitionWindow> windows() { return transitionWindows(p.sequence.clipsOnTrack(A().track)); }
};

}  // namespace

TEST_CASE("transition windows: centred on cuts, inside clips at free edges, clamped") {
    Fixture f;
    f.B().transitionIn = Transition{TransitionType::CrossDissolve, dsec(1)};
    auto w = f.windows();
    CHECK(w.size() == 1);
    CHECK(w[0].start == sec(3.5) && w[0].end == sec(4.5));
    CHECK(w[0].from && w[0].from->id == f.a && w[0].to && w[0].to->id == f.b);
    CHECK(near(static_cast<float>(w[0].progressAt(sec(4))), 0.5f));

    f.B().transitionIn->duration = dsec(30);   // longer than both clips allow
    w = f.windows();
    CHECK(w[0].start == sec(0) && w[0].end == sec(8));

    // A gap before B: the transition comes from underneath, inside B.
    f.B().start = sec(6);
    f.B().transitionIn->duration = dsec(1);
    w = f.windows();
    CHECK(w.size() == 1 && w[0].from == nullptr && w[0].start == sec(6) && w[0].end == sec(7));

    // transitionOut only counts at a free end.
    f.A().transitionOut = Transition{TransitionType::DipToBlack, dsec(2)};
    w = f.windows();
    CHECK(w.size() == 2 && w[0].start == sec(2) && w[0].end == sec(4) && w[0].to == nullptr);
    f.B().start = sec(4);
    w = f.windows();
    CHECK(w.size() == 1 && w[0].from && w[0].to);   // touching again: the cut transition wins
}

TEST_CASE("setTransition / addTransition: one transition per cut, clamped, removable") {
    Fixture f;
    const auto& seq = f.p.sequence;
    // The end of A touches B: it lands on B's In side.
    SequenceEdit e = ops::setTransition(seq, f.a, false, Transition{TransitionType::WipeLeft, dsec(1)});
    CHECK(e.modified.size() == 1 && e.modified[0].id == f.b && e.modified[0].transitionIn.has_value());
    // B's free end: its Out side; 10 s is clamped to the 4 s clip.
    e = ops::setTransition(seq, f.b, false, Transition{TransitionType::CrossDissolve, dsec(10)});
    CHECK(e.modified.size() == 1 && e.modified[0].id == f.b && e.modified[0].transitionOut->duration == dsec(4));
    // A cut allows up to twice the shorter clip.
    e = ops::setTransition(seq, f.b, true, Transition{TransitionType::CrossDissolve, dsec(30)});
    CHECK(e.modified[0].transitionIn->duration == dsec(8));
    // Nothing to remove -> empty edit.
    CHECK(ops::setTransition(seq, f.a, true, std::nullopt).modified.empty());

    // Both clips selected, playhead near the cut: a single transition on B.
    e = ops::addTransition(seq, {f.a, f.b}, sec(3.9), Transition{});
    CHECK(e.modified.size() == 1 && e.modified[0].id == f.b && e.modified[0].transitionIn.has_value());
    // Playhead near A's start: A's In side.
    e = ops::addTransition(seq, {f.a}, sec(0.5), Transition{});
    CHECK(e.modified.size() == 1 && e.modified[0].id == f.a && e.modified[0].transitionIn.has_value());

    f.B().transitionIn = Transition{};
    CHECK(ops::transitionAt(seq, f.a, false).has_value());   // A's end sees B's In
    CHECK(!ops::transitionAt(seq, f.a, true).has_value());
    e = ops::setTransition(seq, f.a, false, std::nullopt);
    CHECK(e.modified.size() == 1 && !e.modified[0].transitionIn.has_value());

    for (auto& t : f.p.sequence.tracks) t.locked = true;
    CHECK(ops::setTransition(seq, f.a, false, Transition{}).modified.empty());
}

TEST_CASE("transitions render through the program: dissolve, dip, wipe, push; handles and freezes") {
    Fixture f;
    f.B().transitionIn = Transition{TransitionType::CrossDissolve, dsec(1)};
    std::vector<playback::VideoLayerRequest> layers;
    {
        playback::SequenceProgram program(f.p);
        program.videoLayersAt(sec(4), layers);
        CHECK(layers.size() == 2);
        if (layers.size() == 2) {
            CHECK(layers[0].layerKey == raw(f.a) && layers[1].layerKey == raw(f.b));
            CHECK(near(layers[1].transform.opacity, 0.5f));
            // A plays on into its media handle; B starts before its In point.
            CHECK(layers[0].sourceTime == sec(4));
            CHECK(layers[1].sourceTime == sec(10));
        }
        layers.clear();
        program.videoLayersAt(sec(3.75), layers);
        CHECK(layers.size() == 2 && layers[1].sourceTime == sec(9.75));
        layers.clear();
        program.videoLayersAt(sec(5), layers);   // after the window: B alone
        CHECK(layers.size() == 1 && layers[0].layerKey == raw(f.b) && layers[0].transform.opacity == 1.0f);
    }
    // No handle before B's media start: B holds its first frame.
    f.B().sourceIn = sec(0);
    {
        playback::SequenceProgram program(f.p);
        layers.clear();
        program.videoLayersAt(sec(3.6), layers);
        CHECK(layers.size() == 2 && layers[1].sourceTime == sec(0));
    }
    f.B().transitionIn->type = TransitionType::DipToWhite;
    {
        playback::SequenceProgram program(f.p);
        layers.clear();
        program.videoLayersAt(sec(3.75), layers);   // first half: A under white at 50%
        CHECK(layers.size() == 2 && layers[0].layerKey == raw(f.a) && layers[1].solid.has_value());
        if (layers.size() == 2) CHECK(layers[1].solid->r == 1.0f && near(layers[1].transform.opacity, 0.5f));
        layers.clear();
        program.videoLayersAt(sec(4.25), layers);   // second half: B under white at 50%
        CHECK(layers.size() == 2 && layers[0].layerKey == raw(f.b) && near(layers[1].transform.opacity, 0.5f));
    }
    f.B().transitionIn->type = TransitionType::WipeRight;
    {
        playback::SequenceProgram program(f.p);
        layers.clear();
        program.videoLayersAt(sec(3.75), layers);
        CHECK(layers.size() == 2 && near(layers[1].crop.right, 0.75f));
    }
    f.B().transitionIn->type = TransitionType::PushLeft;
    {
        playback::SequenceProgram program(f.p);
        layers.clear();
        program.videoLayersAt(sec(4), layers);
        CHECK(layers.size() == 2);
        if (layers.size() == 2) CHECK(near(layers[0].transform.positionX, -960.0f) && near(layers[1].transform.positionX, 960.0f));
    }
    // Fade out at the end of B into what lies underneath.
    f.B().transitionIn.reset();
    f.B().transitionOut = Transition{TransitionType::CrossDissolve, dsec(2)};
    {
        playback::SequenceProgram program(f.p);
        layers.clear();
        program.videoLayersAt(sec(7), layers);
        CHECK(layers.size() == 1 && near(layers[0].transform.opacity, 0.5f));
    }
}

TEST_CASE("split leaves the new cut plain; transitions survive undo/redo and round-trip in files") {
    Document doc;
    Fixture f;
    f.A().transitionIn = Transition{TransitionType::Zoom, dsec(1)};
    f.A().transitionOut = Transition{TransitionType::DipToBlack, dsec(1)};
    doc.reset(f.p);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Split", ops::splitAt(doc, sec(2))));
    const auto& seq = doc.project().sequence;
    const Clip* left = seq.findClip(f.a);
    const Clip* right = nullptr;
    for (const auto& c : seq.clips) if (c.start == sec(2)) right = &c;
    CHECK(left && right);
    if (!left || !right) return;
    CHECK(left->transitionIn.has_value() && !left->transitionOut.has_value());
    CHECK(!right->transitionIn.has_value() && right->transitionOut.has_value());
    doc.undoStack().undo();
    CHECK(seq.findClip(f.a)->transitionOut.has_value());

    const std::string json = serializer::toJson(doc.project(), "");
    auto loaded = serializer::fromJson(json, "");
    CHECK(static_cast<bool>(loaded) && loaded->fileVersion == serializer::kFormatVersion);
    if (!loaded) return;
    CHECK(loaded->project.sequence.findClip(f.a)->transitionIn == seq.findClip(f.a)->transitionIn);
    CHECK(loaded->project.sequence.findClip(f.a)->transitionOut == seq.findClip(f.a)->transitionOut);
    std::string future = json;
    future.replace(future.find("\"zoom\""), 6, "\"warp\"");
    auto f2 = serializer::fromJson(future, "");
    CHECK(static_cast<bool>(f2) && !f2->project.sequence.findClip(f.a)->transitionIn.has_value());
    CHECK(transitionTypeFromId(transitionInfo(TransitionType::PushRight).id) == TransitionType::PushRight);
}

TEST_CASE("whip pan transitions: eased push with a streak that peaks at the cut") {
    Fixture f;
    f.B().transitionIn = Transition{TransitionType::WhipLeft, dsec(1)};
    std::vector<playback::VideoLayerRequest> layers;
    {
        playback::SequenceProgram program(f.p);
        program.videoLayersAt(sec(4), layers);   // the cut = middle of the window
        CHECK(layers.size() == 2);
        if (layers.size() == 2) {
            CHECK(near(layers[0].transform.positionX, -960.0f, 1.0f) && near(layers[1].transform.positionX, 960.0f, 1.0f));
            CHECK(near(layers[0].effects.directionalBlur, 0.22f) && near(layers[1].effects.directionalBlur, 0.22f));
            CHECK(layers[0].effects.directionalBlurAngle == 0.0f);
        }
        layers.clear();
        program.videoLayersAt(sec(3.6), layers);   // 10% in: barely moving, little blur (ease-in)
        CHECK(layers.size() == 2);
        if (layers.size() == 2) {
            CHECK(layers[0].transform.positionX > -20.0f && layers[0].transform.positionX < 0.0f);
            CHECK(layers[0].effects.directionalBlur > 0.0f && layers[0].effects.directionalBlur < 0.03f);
        }
        layers.clear();
        program.videoLayersAt(sec(4.5), layers);   // after the window: B alone, untouched
        CHECK(layers.size() == 1 && layers[0].transform.positionX == 0.0f && layers[0].effects.directionalBlur == 0.0f);
    }
    f.B().transitionIn->type = TransitionType::WhipUp;
    {
        playback::SequenceProgram program(f.p);
        layers.clear();
        program.videoLayersAt(sec(4), layers);
        CHECK(layers.size() == 2);
        if (layers.size() == 2) {
            CHECK(near(layers[0].transform.positionY, -540.0f, 1.0f) && near(layers[1].transform.positionY, 540.0f, 1.0f));
            CHECK(layers[0].effects.directionalBlurAngle == 90.0f && layers[0].transform.positionX == 0.0f);
        }
    }
    CHECK(transitionTypeFromId("whipRight") == TransitionType::WhipRight);
    // Round-trips through the project file.
    const std::string json = serializer::toJson(f.p, "G:/Caches/tmp");
    auto back = serializer::fromJson(json, "G:/Caches/tmp");
    CHECK(static_cast<bool>(back));
    if (back) CHECK(back->project.sequence.findClip(f.b)->transitionIn->type == TransitionType::WhipUp);
}
