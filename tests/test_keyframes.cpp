// V0.4 phase 7: keyframe animation model, evaluation, editing helpers,
// interaction with timeline edits, rendering snapshot and project files.
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
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

Clip textClip(double start, double length) {
    Clip c;
    c.kind = ClipKind::Text;
    c.start = sec(start);
    c.duration = Duration::fromSeconds(length);
    return c;
}

}  // namespace

TEST_CASE("keyframes: evaluate holds outside, interpolates linear / smooth / hold between keys") {
    std::vector<Keyframe> keys{{sec(1), 0.0f, Interp::Linear}, {sec(3), 100.0f, Interp::Smooth}, {sec(5), 0.0f, Interp::Hold}, {sec(7), 50.0f}};
    CHECK(evaluate(keys, sec(0)) == 0.0f);          // before the first key
    CHECK(evaluate(keys, sec(1)) == 0.0f);
    CHECK(near(evaluate(keys, sec(2)), 50.0f));     // linear midpoint
    CHECK(evaluate(keys, sec(3)) == 100.0f);
    CHECK(near(evaluate(keys, sec(4)), 50.0f));     // smooth midpoint is still the midpoint...
    CHECK(evaluate(keys, sec(3.5)) > 75.0f);        // ...but eases: slow start means more of the start value
    CHECK(evaluate(keys, sec(6)) == 0.0f);          // hold until the next key
    CHECK(evaluate(keys, sec(6.999)) == 0.0f);
    CHECK(evaluate(keys, sec(7)) == 50.0f);
    CHECK(evaluate(keys, sec(100)) == 50.0f);       // after the last key
    CHECK(evaluate({}, sec(1)) == 0.0f);
}

TEST_CASE("keyframes: set / replace / remove; last key removal freezes the value; clear keeps the current value") {
    Clip c = textClip(0, 10);
    c.transform.opacity = 1.0f;
    CHECK(!isAnimated(c, AnimProp::Opacity));
    setValueAt(c, AnimProp::Opacity, sec(2), 0.5f);   // not animated: static value changes
    CHECK(c.transform.opacity == 0.5f && c.animation.empty());

    setKey(c, AnimProp::Opacity, sec(4), 0.0f);
    setKey(c, AnimProp::Opacity, sec(2), 1.0f);
    CHECK(isAnimated(c, AnimProp::Opacity));
    const KeyTrack* t = findKeyTrack(c, AnimProp::Opacity);
    CHECK(t && t->keys.size() == 2 && t->keys[0].time == sec(2));   // kept sorted
    setValueAt(c, AnimProp::Opacity, sec(4), 0.25f);                // animated: replaces the key
    CHECK(t->keys.size() == 2 && t->keys[1].value == 0.25f);
    setValueAt(c, AnimProp::Opacity, sec(3), 0.75f);                // animated: inserts a key
    CHECK(t->keys.size() == 3);
    CHECK(keyAt(c, AnimProp::Opacity, sec(3)) != nullptr && keyAt(c, AnimProp::Opacity, sec(3.5)) == nullptr);
    CHECK(near(valueAt(c, AnimProp::Opacity, sec(2.5)), 0.875f));

    // New keys inherit the interpolation of the key before them.
    setKey(c, AnimProp::Opacity, sec(2), 1.0f, Interp::Hold);
    setKey(c, AnimProp::Opacity, sec(2.5), 0.9f);
    CHECK(keyAt(c, AnimProp::Opacity, sec(2.5))->interp == Interp::Hold);

    CHECK(removeKey(c, AnimProp::Opacity, sec(2.5)));
    CHECK(!removeKey(c, AnimProp::Opacity, sec(9)));
    removeKey(c, AnimProp::Opacity, sec(2));
    removeKey(c, AnimProp::Opacity, sec(3));
    CHECK(removeKey(c, AnimProp::Opacity, sec(4)));                // last one
    CHECK(!isAnimated(c, AnimProp::Opacity) && c.transform.opacity == 0.25f);

    setKey(c, AnimProp::PositionX, sec(0), 0.0f);
    setKey(c, AnimProp::PositionX, sec(10), 100.0f);
    clearAnimation(c, AnimProp::PositionX, sec(5));
    CHECK(!isAnimated(c, AnimProp::PositionX) && near(c.transform.positionX, 50.0f));
    CHECK(animPropFromId(animPropId(AnimProp::CropTop)) == AnimProp::CropTop);
    CHECK(animPropFromId("nonsense") == AnimProp::Count);
}

TEST_CASE("keyframes follow the media through trims, splits and speed changes") {
    Clip c;
    c.kind = ClipKind::Video;
    c.start = sec(10);
    c.duration = Duration::fromSeconds(4);
    c.sourceIn = sec(2);
    // Key at media 3 s (sequence 11 s) and media 5 s (sequence 13 s).
    setKey(c, AnimProp::PositionX, sec(3), 0.0f);
    setKey(c, AnimProp::PositionX, sec(5), 200.0f);
    CHECK(c.sequenceTimeAt(sec(3)) == sec(11));
    CHECK(near(valueAt(c, AnimProp::PositionX, c.sourceTimeAt(sec(12))), 100.0f));
    // Moving the clip 5 s later: the same picture still shows the same value.
    c.start = sec(15);
    CHECK(near(valueAt(c, AnimProp::PositionX, c.sourceTimeAt(sec(17))), 100.0f));
    // Double speed: media 4 s now plays at sequence 16 s.
    c.speed = Rational{2, 1};
    CHECK(c.sourceTimeAt(sec(16)) == sec(4));
    CHECK(near(valueAt(c, AnimProp::PositionX, c.sourceTimeAt(sec(16))), 100.0f));
    CHECK(c.sequenceTimeAt(sec(4)) == sec(16));
    // animationTime clamps into the clip.
    c.speed = Rational{1, 1};
    CHECK(animationTime(c, sec(0)) == sec(2));
    CHECK(animationTime(c, sec(100)) < c.sourceOut());
    CHECK(keyTimes(c) == (std::vector<TimePoint>{sec(3), sec(5)}));
}

TEST_CASE("keyframes: split keeps both halves animated; undo/redo of an animated edit") {
    Document doc;
    MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = "C:/media/a.mp4";
    m.displayName = "a.mp4";
    m.info.type = media::MediaType::Video;
    m.info.duration = Duration::fromSeconds(8);
    media::VideoStreamInfo v; v.width = 1280; v.height = 720; v.frameRate = fps::f30; m.info.video = v;
    doc.push(std::make_unique<ImportMediaCommand>(doc, std::vector<MediaItem>{m}));
    auto clips = ops::clipsForMedia(doc, m, ops::defaultPlacement(doc.project(), m, TimePoint::zero()));
    CHECK(clips.size() == 1);
    setKey(clips[0], AnimProp::Opacity, sec(0), 0.0f);
    setKey(clips[0], AnimProp::Opacity, sec(8), 1.0f);
    const ClipId id = clips[0].id;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, std::move(clips))));
    const auto& seq = doc.project().sequence;

    doc.push(std::make_unique<SequenceEditCommand>(doc, "Split", ops::splitAt(doc, sec(4))));
    CHECK(seq.clips.size() == 2);
    for (const auto& c : seq.clips) {
        CHECK(isAnimated(c, AnimProp::Opacity));
        // Either half shows the same opacity for the same picture.
        CHECK(near(valueAt(c, AnimProp::Opacity, c.sourceTimeAt(c.start)), static_cast<float>(c.sourceIn.toSeconds() / 8.0), 1e-3f));
    }

    // An animated edit through a SequenceEditCommand undoes exactly.
    const Clip before = *seq.findClip(id);
    Clip after = before;
    setValueAt(after, AnimProp::Opacity, sec(2), 0.9f);
    SequenceEdit edit;
    edit.modify(after);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Opacity", std::move(edit)));
    CHECK(findKeyTrack(*seq.findClip(id), AnimProp::Opacity)->keys.size() == 3);
    doc.undoStack().undo();
    CHECK(*seq.findClip(id) == before);
    doc.undoStack().redo();
    CHECK(findKeyTrack(*seq.findClip(id), AnimProp::Opacity)->keys.size() == 3);
}

TEST_CASE("keyframes reach the render snapshot (transform, crop) and audio gain") {
    Project p;
    Clip t = textClip(0, 10);
    t.id = ClipId{100};
    t.track = p.sequence.primaryTrack(TrackKind::Video)->id;
    setKey(t, AnimProp::PositionX, sec(0), -100.0f);
    setKey(t, AnimProp::PositionX, sec(10), 100.0f);
    setKey(t, AnimProp::CropLeft, sec(0), 0.9f);    // clamped against the right edge
    t.crop.right = 0.5f;
    p.sequence.clips.push_back(t);
    playback::SequenceProgram program(p);
    std::vector<playback::VideoLayerRequest> layers;
    program.videoLayersAt(sec(5), layers);
    CHECK(layers.size() == 1);
    if (layers.empty()) return;
    CHECK(near(layers[0].transform.positionX, 0.0f));
    CHECK(near(layers[0].crop.left + layers[0].crop.right, 0.98f, 1e-3f));
    layers.clear();
    program.videoLayersAt(sec(7.5), layers);
    CHECK(near(layers[0].transform.positionX, 50.0f));

    Clip a;
    a.kind = ClipKind::Audio;
    a.start = sec(0);
    a.duration = Duration::fromSeconds(4);
    setKey(a, AnimProp::Volume, sec(0), 0.0f);
    setKey(a, AnimProp::Volume, sec(4), 1.0f);
    CHECK(near(playback::SequenceProgram::gainAt(a, sec(1)), 0.25f));
    a.audio.muted = true;
    CHECK(playback::SequenceProgram::gainAt(a, sec(1)) == 0.0f);
}

TEST_CASE("serializer v5: keyframes round-trip; unknown properties are dropped") {
    Project p;
    Clip t = textClip(0, 5);
    t.id = ClipId{7};
    t.track = p.sequence.primaryTrack(TrackKind::Video)->id;
    setKey(t, AnimProp::Rotation, sec(0), 0.0f, Interp::Smooth);
    setKey(t, AnimProp::Rotation, sec(5), 360.0f, Interp::Hold);
    setKey(t, AnimProp::Opacity, sec(1), 0.5f);
    p.sequence.clips.push_back(t);
    std::string json = serializer::toJson(p, "");
    auto loaded = serializer::fromJson(json, "");
    CHECK(static_cast<bool>(loaded));
    if (!loaded) return;
    CHECK(loaded->fileVersion == 5);
    const Clip* c = loaded->project.sequence.findClip(ClipId{7});
    CHECK(c && c->animation == t.animation);

    const auto pos = json.find("\"rotation\"", json.find("\"animation\""));
    CHECK(pos != std::string::npos);
    json.replace(pos, 10, "\"warpDrive\"");
    auto future = serializer::fromJson(json, "");
    CHECK(static_cast<bool>(future));
    if (future) CHECK(future->project.sequence.findClip(ClipId{7})->animation.size() == 1);
}
