#include "TestMain.h"

#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"

#include <filesystem>
#include <fstream>

using namespace lf;
using namespace lf::project;
namespace fs = std::filesystem;

namespace {

std::string mediaPath(const char* name) { return std::string(LF_TEST_MEDIA_DIR) + "/" + name; }

fs::path tempDir() {
    const fs::path d = fs::path(LF_TEST_MEDIA_DIR).parent_path() / "tmp_serializer";
    fs::create_directories(d);
    return d;
}

Project sampleProject() {
    Document doc;
    MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = mediaPath("countdown_720p30.mp4");
    m.displayName = "countdown";
    m.info.path = m.path;
    m.info.type = media::MediaType::Video;
    m.info.duration = Duration::fromSeconds(6);
    m.info.fileSize = 2107459;
    media::VideoStreamInfo v;
    v.width = 1280; v.height = 720; v.frameRate = fps::f30; v.codecName = "h264"; v.timeBase = Rational{1, 15360};
    m.info.video = v;
    media::AudioStreamInfo a;
    a.channels = 1; a.sampleRate = 48000; a.codecName = "aac";
    m.info.audio = a;
    m.fingerprintSize = 2107459;
    m.fingerprintMtime = 1700000000;
    doc.push(std::make_unique<ImportMediaCommand>(doc, std::vector<MediaItem>{m}));
    auto clips = ops::clipsForMedia(doc, m, ops::defaultPlacement(doc.project(), m, TimePoint::zero()));
    clips[0].transform.positionX = 12.5f;
    clips[0].transform.scale = 0.75f;
    clips[0].color.saturation = 1.3f;
    clips[0].effects.grayscale = true;
    clips[0].speed = Rational{3, 2};
    clips[0].duration = Duration::fromSeconds(4);
    clips[1].audio.volume = 0.5f;
    clips[1].audio.fadeIn = Duration::fromSeconds(1);
    clips[1].duration = Duration::fromSeconds(4);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, clips)));
    // A text clip on V2
    Clip t;
    t.id = doc.allocateClipId();
    t.track = doc.project().sequence.tracks[0].id;
    t.kind = ClipKind::Text;
    t.name = "Title";
    t.start = TimePoint::fromSeconds(1);
    t.duration = Duration::fromSeconds(3);
    t.text.text = "Hello, world";
    t.text.fontSize = 96;
    t.text.outlineWidth = 3;
    t.text.shadow = true;
    t.text.align = TextAlign::Left;
    SequenceEdit e;
    e.add(t);
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add Text", e));
    doc.project().sequence.tracks[3].muted = true;
    doc.project().settings.name = "Round trip";
    doc.project().settings.frameRate = fps::f29_97;
    return doc.project();
}

}  // namespace

TEST_CASE("project JSON round-trips every field") {
    const Project p = sampleProject();
    const std::string json = serializer::toJson(p, LF_TEST_MEDIA_DIR);
    auto loaded = serializer::fromJson(json, LF_TEST_MEDIA_DIR);
    CHECK(loaded.ok());
    if (!loaded) { std::printf("     %s\n", loaded.error().fullText().c_str()); return; }
    const Project& q = loaded->project;
    CHECK(q.settings == p.settings);
    CHECK(q.media == p.media);
    CHECK(q.sequence.tracks == p.sequence.tracks);
    CHECK(q.sequence.clips == p.sequence.clips);
    CHECK(loaded->offlineMedia.empty());
}

TEST_CASE("project save/load through a file, with relative media resolution when moved") {
    const Project p = sampleProject();
    const fs::path dir = tempDir();
    const fs::path file = dir / "roundtrip.lfproj";
    auto saved = serializer::saveToFile(p, file.string());
    CHECK(saved.ok());
    CHECK(fs::exists(file));
    CHECK(!fs::exists(dir / "roundtrip.lfproj.tmp"));
    auto loaded = serializer::loadFromFile(file.string());
    CHECK(loaded.ok());
    if (loaded) CHECK(loaded->project.sequence.clips.size() == 3);

    // Break the absolute path but keep the relative one: the media must still resolve.
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::string abs = p.media[0].path;   // forward slashes: appears verbatim in the JSON
    int replaced = 0;
    for (auto pos = text.find(abs); pos != std::string::npos; pos = text.find(abs)) { text.replace(pos, abs.size(), "Z:/nowhere/countdown_720p30.mp4"); ++replaced; }
    CHECK(replaced >= 1);
    auto moved = serializer::fromJson(text, dir.string());
    CHECK(moved.ok());
    if (moved) {
        CHECK(!moved->project.media[0].offline);
        if (!moved->project.media[0].offline) CHECK(fs::equivalent(fs::path(moved->project.media[0].path), fs::path(abs)));
    }
    // No relative fallback possible: marked offline, clips kept.
    auto offline = serializer::fromJson(text, "Q:/elsewhere");
    CHECK(offline.ok());
    if (offline) {
        CHECK(offline->project.media[0].offline);
        CHECK(offline->offlineMedia.size() == 1);
        CHECK(offline->project.sequence.clips.size() == 3);
    }
    fs::remove_all(dir);
}

TEST_CASE("serializer rejects garbage, foreign files and newer versions with clear messages") {
    CHECK(!serializer::fromJson("not json", "").ok());
    CHECK(!serializer::fromJson("{\"format\":\"something-else\",\"version\":1}", "").ok());
    auto newer = serializer::fromJson("{\"format\":\"lumaforge-project\",\"version\":99}", "");
    CHECK(!newer.ok());
    if (!newer) CHECK(newer.error().message.find("newer version") != std::string::npos);
    CHECK(!serializer::loadFromFile("Q:/does/not/exist.lfproj").ok());
}

TEST_CASE("loader drops clips that reference unknown tracks or media and repairs broken links") {
    const char* text = R"({
      "format": "lumaforge-project", "version": 1,
      "settings": {"name": "x", "width": 1280, "height": 720, "frameRate": [25, 1]},
      "media": [],
      "sequence": {
        "tracks": [{"id": 1, "kind": "video", "name": "V1"}],
        "clips": [
          {"id": 10, "track": 1, "kind": "text", "name": "ok", "start": 0, "duration": 100, "link": 11, "text": {"text": "hi"}},
          {"id": 11, "track": 2, "kind": "text", "name": "bad track", "start": 0, "duration": 100},
          {"id": 12, "track": 1, "kind": "video", "media": 5, "name": "bad media", "start": 0, "duration": 100}
        ]
      }
    })";
    auto loaded = serializer::fromJson(text, "");
    CHECK(loaded.ok());
    if (!loaded) return;
    CHECK(loaded->project.sequence.clips.size() == 1);
    CHECK(loaded->project.sequence.clips[0].link == ClipId::Invalid);
    CHECK(loaded->project.sequence.clips[0].text.text == "hi");
    CHECK(loaded->project.settings.frameRate == fps::f25);
}

TEST_CASE("document reset from a loaded project restores id counters and clean state") {
    Document doc;
    auto loaded = serializer::fromJson(serializer::toJson(sampleProject(), LF_TEST_MEDIA_DIR), LF_TEST_MEDIA_DIR);
    CHECK(loaded.ok());
    if (!loaded) return;
    const auto maxClip = [&] { std::uint64_t m = 0; for (const auto& c : loaded->project.sequence.clips) m = std::max(m, raw(c.id)); return m; }();
    doc.reset(std::move(loaded->project));
    CHECK(!doc.isDirty());
    CHECK(raw(doc.allocateClipId()) == maxClip + 1);
    CHECK(raw(doc.allocateTrackId()) == 5);
}
