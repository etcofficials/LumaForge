// V0.4 phases 9-11: masks and generated (solid / adjustment) clips through the
// program snapshot and project files; sequence markers.
#include "TestMain.h"

#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"

using namespace lf;
using namespace lf::project;

namespace {
TimePoint sec(double s) { return TimePoint::fromSeconds(s); }

Clip generated(ClipKind kind, ClipId id, TrackId track, double start, double length) {
    Clip c;
    c.id = id;
    c.track = track;
    c.kind = kind;
    c.start = sec(start);
    c.duration = Duration::fromSeconds(length);
    return c;
}
}  // namespace

TEST_CASE("solid and adjustment clips render without media; masks reach the layer") {
    Project p;
    const auto tracks = p.sequence.videoTracksBottomUp();
    CHECK(tracks.size() == 2);
    Clip solid = generated(ClipKind::Solid, ClipId{1}, tracks[0]->id, 0, 5);
    solid.solidColor = {0.2f, 0.4f, 0.6f, 1.0f};
    solid.mask.shape = MaskShape::Rectangle;
    solid.mask.width = 0.25f;
    Clip adj = generated(ClipKind::Adjustment, ClipId{2}, tracks[1]->id, 1, 2);
    adj.effects.push_back(makeEffect(EffectType::Grayscale));
    p.sequence.clips = {solid, adj};
    CHECK(isGeneratedClip(ClipKind::Solid) && isGeneratedClip(ClipKind::Adjustment) && !isGeneratedClip(ClipKind::Image));

    playback::SequenceProgram program(p);
    std::vector<playback::VideoLayerRequest> layers;
    program.videoLayersAt(sec(1.5), layers);
    CHECK(layers.size() == 2);
    if (layers.size() != 2) return;
    CHECK(layers[0].path.empty() && layers[0].solid.has_value() && layers[0].solid->g == 0.4f);
    CHECK(layers[0].mask.shape == MaskShape::Rectangle && layers[0].mask.width == 0.25f);
    CHECK(layers[1].path.empty() && layers[1].adjustment && layers[1].effects.grayscale == 1.0f);
    layers.clear();
    program.videoLayersAt(sec(4), layers);
    CHECK(layers.size() == 1 && !layers[0].adjustment);
}

TEST_CASE("markers: add (no duplicates), rename/recolour/move keep order, remove, navigate; undo/redo") {
    Document doc;
    const auto& seq = doc.project().sequence;
    auto apply = [&doc](const char* text, SequenceEdit e) { CHECK(!e.empty()); doc.push(std::make_unique<SequenceEditCommand>(doc, text, std::move(e))); };
    int notified = 0;
    doc.addListener([&](const Change& c) { if (c.kind == Change::Kind::MarksChanged) ++notified; });

    apply("Add", ops::addMarker(seq, sec(10)));
    apply("Add", ops::addMarker(seq, sec(2), "Intro"));
    CHECK(ops::addMarker(seq, sec(10)).empty());          // one marker per time
    CHECK(seq.markers.size() == 2 && seq.markers[0].name == "Intro" && seq.markers[1].name == "Marker 1");
    CHECK(seq.markers[0].id != seq.markers[1].id);

    Marker m = seq.markers[1];
    m.time = sec(1);
    m.color = 4;
    m.name = "Cold open";
    apply("Update", ops::updateMarker(seq, m));
    CHECK(seq.markers[0].name == "Cold open" && seq.markers[0].color == 4);   // re-sorted
    CHECK(ops::updateMarker(seq, seq.markers[0]).empty());                     // unchanged

    CHECK(ops::nextMarker(seq, sec(1)) == sec(2));
    CHECK(!ops::nextMarker(seq, sec(2)).has_value());
    CHECK(ops::previousMarker(seq, sec(2)) == sec(1));
    CHECK(!ops::previousMarker(seq, sec(1)).has_value());
    CHECK(ops::snap(sec(2.01), ops::snapTargets(seq, {}, sec(50)), Duration::fromSeconds(0.1)) == sec(2));

    apply("Remove", ops::removeMarkers(seq, {seq.markers[0].id}));
    CHECK(seq.markers.size() == 1 && seq.markers[0].name == "Intro");
    CHECK(notified == 4);
    doc.undoStack().undo();
    CHECK(seq.markers.size() == 2 && seq.markers[0].name == "Cold open");
    doc.undoStack().undo();
    CHECK(seq.markers[1].name == "Marker 1" && seq.markers[1].time == sec(10));
    doc.undoStack().redo();
    doc.undoStack().redo();
    CHECK(seq.markers.size() == 1);
}

TEST_CASE("markers: YouTube chapter list format") {
    Sequence s;
    s.markers = {{1, sec(0), "Intro", 0, {}}, {2, sec(83), "Setup", 0, {}}, {3, sec(615), "Results", 0, {}}};
    CHECK(ops::chapterList(s) == "0:00 Intro\n1:23 Setup\n10:15 Results\n");
    s.markers.push_back({4, sec(3725), "Outro", 0, {}});
    CHECK(ops::chapterList(s) == "0:00:00 Intro\n0:01:23 Setup\n0:10:15 Results\n1:02:05 Outro\n");
}

TEST_CASE("serializer v7: masks, solid / adjustment clips and markers round-trip") {
    Project p;
    const auto tracks = p.sequence.videoTracksBottomUp();
    Clip solid = generated(ClipKind::Solid, ClipId{1}, tracks[0]->id, 0, 5);
    solid.solidColor = {1.0f, 0.5f, 0.25f, 1.0f};
    solid.mask = Mask{MaskShape::Ellipse, 0.3f, 0.6f, 0.4f, 0.2f, 0.05f, true};
    Clip adj = generated(ClipKind::Adjustment, ClipId{2}, tracks[1]->id, 0, 5);
    p.sequence.clips = {solid, adj};
    p.sequence.markers = {{1, sec(1), "A", 2, "note"}, {2, sec(3), "B", 0, {}}};
    auto loaded = serializer::fromJson(serializer::toJson(p, ""), "");
    CHECK(static_cast<bool>(loaded));
    if (!loaded) return;
    CHECK(loaded->fileVersion == serializer::kFormatVersion);
    const auto& ls = loaded->project.sequence;
    CHECK(ls.clips.size() == 2);   // generated clips need no media
    CHECK(ls.findClip(ClipId{1})->kind == ClipKind::Solid && ls.findClip(ClipId{1})->solidColor == solid.solidColor);
    CHECK(ls.findClip(ClipId{1})->mask == solid.mask);
    CHECK(ls.findClip(ClipId{2})->kind == ClipKind::Adjustment);
    CHECK(ls.markers == p.sequence.markers);
}
