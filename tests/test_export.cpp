// Export pipeline: settings and validation, a real H.264/AAC MP4 render read
// back with the prober, cancellation, colour fidelity, single-frame snapshots.
#include "TestMain.h"

#include "lumaforge/export/Exporter.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/media/MediaProbe.h"
#include "lumaforge/media/Thumbnailer.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"
#include "lumaforge/project/TitlePresets.h"

#include <atomic>
#include <cmath>
#include <filesystem>

using namespace lf;
using namespace lf::project;
using namespace lf::exporting;

namespace {

std::string mediaPath(const char* name) { return std::string(LF_TEST_MEDIA_DIR) + "/" + name; }
const std::string kOutDir = "G:/Caches/tmp/lf_export_tests";

// A project with countdown_720p30.mp4 (6 s, H.264 + AAC) on V1/A1.
Project countdownProject() {
    Document doc;
    auto info = media::MediaProbe::probe(mediaPath("countdown_720p30.mp4"));
    CHECK(static_cast<bool>(info));
    MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = mediaPath("countdown_720p30.mp4");
    m.displayName = "countdown";
    if (info) m.info = *info;
    doc.push(std::make_unique<ImportMediaCommand>(doc, std::vector<MediaItem>{m}));
    auto clips = ops::clipsForMedia(doc, m, ops::defaultPlacement(doc.project(), m, TimePoint::zero()));
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, std::move(clips))));
    Project p = doc.project();
    p.settings.width = 1280;
    p.settings.height = 720;
    p.settings.frameRate = fps::f30;
    return p;
}

ExportSettings smallSettings(const std::string& name) {
    std::filesystem::create_directories(kOutDir);
    ExportSettings s;
    s.outputPath = kOutDir + "/" + name;
    s.width = 640;
    s.height = 360;
    s.speedPreset = "ultrafast";
    s.crf = 23;
    return s;
}

std::shared_ptr<hw::GpuDevice> gpu() {
    auto g = hw::GpuDevice::create({});
    return g ? std::move(g.value()) : nullptr;
}

}  // namespace

TEST_CASE("export settings: presets, sizes, ranges, validation, offline media") {
    Project p = countdownProject();
    ExportSettings s;
    s.outputPath = "G:/Caches/tmp/out.mp4";
    int w = 0, h = 0;
    outputSize(s, p, w, h);
    CHECK(w == 1280 && h == 720);
    for (const auto& preset : exportPresets()) {
        applyPreset(s, preset, p);
        CHECK(s.outputPath.ends_with(containerExtension(preset.container)));
        outputSize(s, p, w, h);
        CHECK(w % 2 == 0 && h % 2 == 0 && w <= 1280 && h <= 720);
    }
    applyPreset(s, exportPresets()[3], p);   // 480p web
    outputSize(s, p, w, h);
    CHECK(h == 480 && w == 854);
    CHECK(s.rateControl == RateControl::Bitrate);

    s = ExportSettings{};
    s.outputPath = kOutDir + "/x.mp4";
    std::filesystem::create_directories(kOutDir);
    CHECK(validate(s, p).empty());
    CHECK(frameCount(s, p) == 180);                       // 6 s at 30 fps
    s.rangeStart = TimePoint::fromSeconds(1);
    s.rangeEnd = TimePoint::fromSeconds(2.5);
    CHECK(frameCount(s, p) == 45);
    CHECK(exportRange(s, p).duration == Duration::fromSeconds(1.5));
    s.rangeEnd = TimePoint::fromSeconds(1);
    CHECK(!validate(s, p).empty());                       // empty range
    s = ExportSettings{};
    s.outputPath = kOutDir + "/x.mkv";
    CHECK(!validate(s, p).empty());                       // extension does not match MP4
    s.outputPath = "Z:/no/such/folder/x.mp4";
    CHECK(!validate(s, p).empty());
    CHECK(!validate(ExportSettings{}, Project{}).empty()); // empty timeline, no path
    CHECK(estimatedBytes(s, p) > 0);

    CHECK(offlineMediaInRange(s, p).empty());
    p.media[0].offline = true;
    CHECK(offlineMediaInRange(s, p).size() == 1);
}

TEST_CASE("export: H.264 + AAC MP4 from the timeline (title + transition) reads back correctly") {
    auto g = gpu();
    Project p = countdownProject();
    // A title over the first second, fading in.
    Clip title = makeTitleClip(0, p.settings.width, p.settings.height);
    title.id = ClipId{900};
    title.track = p.sequence.videoTracksBottomUp().back()->id;
    title.start = TimePoint::zero();
    title.duration = Duration::fromSeconds(1);
    title.transitionIn = Transition{TransitionType::CrossDissolve, Duration::fromSeconds(0.5)};
    p.sequence.clips.push_back(title);

    ExportSettings s = smallSettings("timeline.mp4");
    s.rangeEnd = TimePoint::fromSeconds(2);
    std::filesystem::remove(s.outputPath);
    Exporter exporter(g);
    std::atomic<bool> cancel{false};
    int reports = 0;
    std::int64_t lastFrame = 0;
    auto result = exporter.run(p, s, [&](const ExportProgress& pr) { ++reports; lastFrame = pr.frame; CHECK(pr.totalFrames == 60); }, cancel);
    CHECK(static_cast<bool>(result));
    if (!result) { std::printf("    export error: %s\n", result.error().fullText().c_str()); return; }
    CHECK(reports > 0 && lastFrame == 60);
    CHECK(result->frames == 60 && result->videoEncoder == "libx264" && result->audioEncoder == "aac");
    CHECK(std::filesystem::exists(s.outputPath));
    CHECK(!std::filesystem::exists(kOutDir + "/timeline.partial.mp4"));
    auto probe = media::MediaProbe::probe(s.outputPath);
    CHECK(static_cast<bool>(probe));
    if (!probe) return;
    CHECK(probe->hasVideo() && probe->hasAudio());
    CHECK(probe->video->width == 640 && probe->video->height == 360);
    CHECK(probe->video->codecName == "h264");
    CHECK(std::abs(probe->duration.toSeconds() - 2.0) < 0.15);
    CHECK(std::abs(probe->video->frameRate.toDouble() - 30.0) < 0.01);
}

TEST_CASE("export: cancel leaves no file; a solid colour survives encoding") {
    auto g = gpu();
    Project p = countdownProject();
    ExportSettings s = smallSettings("cancelled.mp4");
    std::filesystem::remove(s.outputPath);
    Exporter exporter(g);
    std::atomic<bool> cancel{false};
    auto result = exporter.run(p, s, [&](const ExportProgress& pr) { if (pr.frame >= 5) cancel = true; }, cancel);
    CHECK(!result);
    CHECK(!std::filesystem::exists(s.outputPath));
    CHECK(!std::filesystem::exists(kOutDir + "/cancelled.partial.mp4"));

    // Solid colour only (no audio): the decoded file matches the colour.
    Project solid;
    solid.settings.width = 320;
    solid.settings.height = 180;
    Clip c;
    c.id = ClipId{1};
    c.track = solid.sequence.primaryTrack(TrackKind::Video)->id;
    c.kind = ClipKind::Solid;
    c.duration = Duration::fromSeconds(1);
    c.solidColor = {0.8f, 0.2f, 0.1f, 1.0f};
    solid.sequence.clips.push_back(c);
    ExportSettings s2 = smallSettings("solid.mp4");
    s2.width = s2.height = 0;
    s2.crf = 10;
    std::atomic<bool> no{false};
    auto r2 = exporter.run(solid, s2, {}, no);
    CHECK(static_cast<bool>(r2));
    if (!r2) { std::printf("    export error: %s\n", r2.error().fullText().c_str()); return; }
    CHECK(r2->audioEncoder.empty());
    media::Thumbnailer::Options o;
    o.at = TimePoint::fromSeconds(0.5);
    auto img = media::Thumbnailer::render(s2.outputPath, o);
    CHECK(static_cast<bool>(img));
    if (!img) return;
    const std::size_t i = (static_cast<std::size_t>(img->height / 2) * img->width + img->width / 2) * 4;
    const int b = img->bgra[i], gr = img->bgra[i + 1], r = img->bgra[i + 2];
    CHECK(std::abs(r - 204) <= 8 && std::abs(gr - 51) <= 8 && std::abs(b - 26) <= 8);
}

TEST_CASE("export: single-frame snapshot at project size") {
    auto g = gpu();
    Project p = countdownProject();
    Exporter exporter(g);
    auto img = exporter.renderFrame(p, TimePoint::fromSeconds(1), 320, 180);
    CHECK(static_cast<bool>(img));
    if (!img) return;
    CHECK(img->width == 320 && img->height == 180 && img->bgra.size() == 320u * 180u * 4u);
    // The countdown is a colourful picture: not all black.
    int lit = 0;
    for (std::size_t i = 0; i < img->bgra.size(); i += 4) lit += (img->bgra[i] + img->bgra[i + 1] + img->bgra[i + 2]) > 60;
    CHECK(lit > 320 * 180 / 2);
}

TEST_CASE("export: an encoder that cannot start falls back to x264 and says so") {
    auto g = gpu();
    Project p = countdownProject();
    ExportSettings s = smallSettings("fallback.mp4");
    s.rangeEnd = TimePoint::fromSeconds(0.5);
    s.encoder = "h264_this_encoder_does_not_exist";
    Exporter exporter(g);
    std::atomic<bool> no{false};
    auto r = exporter.run(p, s, {}, no);
    CHECK(static_cast<bool>(r));
    if (r) CHECK(r->videoEncoder == "libx264" && !r->note.empty());
}
