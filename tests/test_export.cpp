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

#include "lumaforge/export/Proxy.h"
#include "lumaforge/playback/SequenceProgram.h"

TEST_CASE("proxies: deterministic names, a real 540p transcode with the same timing, used only when asked") {
    Project p = countdownProject();
    const MediaItem& item = p.media[0];
    CHECK(proxyApplies(item));
    CHECK(proxyFileName(item) == proxyFileName(item));
    MediaItem moved = item;
    moved.fingerprintMtime += 1;   // the source changed: a different proxy
    CHECK(proxyFileName(moved) != proxyFileName(item));
    CHECK(proxyFileName(item).ends_with("_540p.mp4"));

    const std::string out = kOutDir + "/proxy_" + proxyFileName(item, 360);
    std::filesystem::remove(out);
    std::atomic<bool> cancel{false};
    double last = 0;
    auto made = makeProxy(item.path, out, 360, [&](double f) { last = f; }, cancel);
    CHECK(static_cast<bool>(made));
    if (!made) { std::printf("    proxy error: %s\n", made.error().fullText().c_str()); return; }
    CHECK(last == 1.0);
    auto src = media::MediaProbe::probe(item.path);
    auto proxy = media::MediaProbe::probe(out);
    CHECK(src && proxy && proxy->video && !proxy->audio);
    if (!src || !proxy || !proxy->video) return;
    CHECK(proxy->video->height == 360 && proxy->video->width == 640);
    CHECK(std::abs(proxy->duration.toSeconds() - src->duration.toSeconds()) < 0.15);

    // The program swaps the path only for overridden media.
    playback::SequenceProgram plain(p);
    playback::SequenceProgram withProxy(p, {{item.id, out}});
    std::vector<playback::VideoLayerRequest> a, b;
    plain.videoLayersAt(TimePoint::fromSeconds(1), a);
    withProxy.videoLayersAt(TimePoint::fromSeconds(1), b);
    CHECK(a.size() == 1 && b.size() == 1 && a[0].path == item.path && b[0].path == out && a[0].sourceTime == b[0].sourceTime);
    std::vector<playback::AudioClipRequest> audio;
    withProxy.audioClipsAt(TimePoint::fromSeconds(1), audio);
    CHECK(audio.size() == 1 && audio[0].path == item.path);   // sound from the original

    // Cancelling leaves nothing behind.
    std::atomic<bool> stop{true};
    const std::string cancelled = kOutDir + "/proxy_cancelled.mp4";
    CHECK(!makeProxy(item.path, cancelled, 360, {}, stop));
    CHECK(!std::filesystem::exists(cancelled) && !std::filesystem::exists(kOutDir + "/proxy_cancelled.partial.mp4"));
}

#include "lumaforge/export/FastCut.h"

TEST_CASE("export: frame-rate conversion, mono audio, metadata; lossless fast cut only when nothing is processed") {
    auto g = gpu();
    Project p = countdownProject();   // 30 fps source
    ExportSettings s = smallSettings("25fps.mp4");
    s.frameRate = Rational{25, 1};
    s.audioChannels = 1;
    s.title = "Test title";
    s.rangeEnd = TimePoint::fromSeconds(2);
    CHECK(frameCount(s, p) == 50);
    Exporter exporter(g);
    std::atomic<bool> no{false};
    auto r = exporter.run(p, s, {}, no);
    CHECK(static_cast<bool>(r));
    if (!r) { std::printf("    export error: %s\n", r.error().fullText().c_str()); return; }
    auto probe = media::MediaProbe::probe(s.outputPath);
    CHECK(probe && probe->video && std::abs(probe->video->frameRate.toDouble() - 25.0) < 0.01);
    CHECK(probe && probe->audio && probe->audio->channels == 1);
    CHECK(probe && std::abs(probe->duration.toSeconds() - 2.0) < 0.15);

    // Fast cut: eligible for the untouched clip...
    ExportSettings fc = smallSettings("lossless.mp4");
    fc.losslessFastCut = true;
    fc.rangeStart = TimePoint::fromSeconds(1);
    fc.rangeEnd = TimePoint::fromSeconds(4);
    auto check = checkFastCut(p, fc);
    CHECK(check.eligible);
    auto lossless = exporter.run(p, fc, {}, no);
    CHECK(static_cast<bool>(lossless));
    if (lossless) {
        CHECK(lossless->videoEncoder == "copy");
        auto lp = media::MediaProbe::probe(fc.outputPath);
        auto src = media::MediaProbe::probe(p.media[0].path);
        CHECK(lp && src && lp->video->width == src->video->width && lp->video->codecName == src->video->codecName);   // untouched stream
        CHECK(lp && lp->duration.toSeconds() >= 2.9 && lp->duration.toSeconds() < 4.5);   // keyframe-aligned start
        CHECK(lp && lp->hasAudio());
    }
    // ...but not once anything changes the picture.
    for (auto& c : p.sequence.clips) if (c.kind == ClipKind::Video) c.color.saturation = 0.0f;
    check = checkFastCut(p, fc);
    CHECK(!check.eligible && check.reason.find("colour") != std::string::npos);
    CHECK(!exporter.run(p, fc, {}, no));
}

#include "lumaforge/playback/AudioAnalysis.h"
#include "lumaforge/project/ProjectSerializer.h"

#include <algorithm>

namespace {
media::Image frameAt(const std::string& path, double seconds) {
    media::Thumbnailer::Options o;
    o.maxWidth = 640;
    o.maxHeight = 360;
    o.at = TimePoint::fromSeconds(seconds);
    auto img = media::Thumbnailer::render(path, o);
    return img ? *img : media::Image{};
}
double lumaAt(const media::Image& img, int x, int y) {
    const auto* p = &img.bgra[(static_cast<std::size_t>(y) * img.width + x) * 4];
    return 0.2126 * p[2] + 0.7152 * p[1] + 0.0722 * p[0];
}
double meanLuma(const media::Image& img, int x0, int y0, int x1, int y1) {
    double sum = 0;
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x, ++n) sum += lumaAt(img, x, y);
    return n ? sum / n : 0;
}
// Mean of (max - min) over the RGB channels: ~0 for a grey picture.
double meanChroma(const media::Image& img, int x0, int y0, int x1, int y1) {
    double sum = 0;
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x, ++n) {
            const auto* p = &img.bgra[(static_cast<std::size_t>(y) * img.width + x) * 4];
            sum += std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
        }
    return n ? sum / n : 0;
}
}  // namespace

TEST_CASE("export acceptance (spec 80.30): edit, save, reload, export - the MP4 shows every edit") {
    auto g = gpu();
    Document doc;
    auto addMedia = [&](const char* file) {
        MediaItem m;
        m.id = doc.allocateMediaId();
        m.path = mediaPath(file);
        m.displayName = file;
        auto info = media::MediaProbe::probe(m.path);
        CHECK(static_cast<bool>(info));
        if (info) m.info = *info;
        doc.push(std::make_unique<ImportMediaCommand>(doc, std::vector<MediaItem>{m}));
        return m;
    };
    // 1. Import video (and the audio used in step 11).
    const MediaItem video = addMedia("countdown_720p30.mp4");   // 6 s colour bars, 30 fps
    const MediaItem tone = addMedia("tone_44k.wav");            // 3 s sine, mono 44.1 kHz
    doc.project().settings.width = 1280;
    doc.project().settings.height = 720;
    doc.project().settings.frameRate = fps::f30;
    auto apply = [&](SequenceEdit e) {
        CHECK(!e.empty());
        doc.push(std::make_unique<SequenceEditCommand>(doc, "edit", std::move(e)));
    };
    const auto& seq = doc.project().sequence;
    // 2. Add to the timeline (V1 + A1 at 0).
    apply(ops::insertClips(doc, ops::clipsForMedia(doc, video, ops::defaultPlacement(doc.project(), video, TimePoint::zero()))));
    // 4-5. Split at 2 s and ripple-delete the first section.
    apply(ops::splitAt(doc, TimePoint::fromSeconds(2)));
    std::vector<ClipId> first;
    for (const auto& c : seq.clips)
        if (c.start == TimePoint::zero()) first.push_back(c.id);
    CHECK(first.size() == 2);
    apply(ops::removeClips(doc, first, true));
    ClipId v = ClipId::Invalid;
    for (const auto& c : seq.clips)
        if (c.kind == ClipKind::Video) v = c.id;
    CHECK(seq.findClip(v) && seq.findClip(v)->start == TimePoint::zero() && seq.findClip(v)->sourceIn == TimePoint::fromSeconds(2));
    // 6. Trim the remaining section to 3 s (its audio follows).
    apply(ops::trimClip(doc, v, ops::Edge::End, TimePoint::fromSeconds(3)));
    // 7. Move it to 0.5 s.
    auto moved = ops::moveClips(doc, ops::withLinked(seq, {v}), Duration::fromSeconds(0.5), 0);
    CHECK(moved.has_value());
    if (moved) apply(std::move(*moved));
    CHECK(seq.findClip(v)->start == TimePoint::fromSeconds(0.5) && seq.findClip(v)->duration == Duration::fromSeconds(3));
    // 8. Colour: fully desaturated. 13. Transition: fade in from black over 0.5 s.
    {
        Clip c = *seq.findClip(v);
        c.color.saturation = 0.0f;
        SequenceEdit e;
        e.modified.push_back(c);
        apply(std::move(e));
        apply(ops::setTransition(seq, v, true, Transition{TransitionType::CrossDissolve, Duration::fromSeconds(0.5)}));
        CHECK(ops::transitionAt(seq, v, true).has_value());
    }
    // 9-10. Text over 1.0..2.5 s, moved 250 px up.
    {
        Clip t = makeTitleClip(0, 1280, 720);
        t.id = doc.allocateClipId();
        t.track = seq.videoTracksBottomUp().back()->id;
        t.start = TimePoint::fromSeconds(1.0);
        t.duration = Duration::fromSeconds(1.5);
        t.text.text = "LUMAFORGE";
        t.transform.positionY = -250.0f;
        apply(ops::insertClips(doc, {t}));
    }
    // 11-12. Audio: the tone on A2 at half volume. The video's own sound is muted so the level is measurable.
    {
        ops::Placement place;
        place.audioTrack = seq.audioTracks().back()->id;
        place.start = TimePoint::fromSeconds(0.5);
        auto clips = ops::clipsForMedia(doc, tone, place);
        CHECK(clips.size() == 1);
        for (auto& c : clips) c.audio.volume = 0.5f;
        apply(ops::insertClips(doc, std::move(clips)));
        SequenceEdit e;
        for (const auto& c : seq.clips)
            if (c.kind == ClipKind::Audio && c.media == video.id) {
                Clip m = c;
                m.audio.muted = true;
                e.modified.push_back(m);
            }
        apply(std::move(e));
    }
    CHECK(seq.duration() == Duration::fromSeconds(3.5));
    // 14. Save and reload.
    std::filesystem::create_directories(kOutDir);
    const std::string projectFile = kOutDir + "/acceptance.lfproj";
    CHECK(static_cast<bool>(serializer::saveToFile(doc.project(), projectFile)));
    auto loaded = serializer::loadFromFile(projectFile);
    CHECK(static_cast<bool>(loaded));
    if (!loaded) return;
    CHECK(loaded->project.sequence.clips.size() == seq.clips.size());
    // 15. Export MP4.
    ExportSettings s = smallSettings("acceptance.mp4");
    s.crf = 18;
    Exporter exporter(g);
    std::atomic<bool> no{false};
    auto r = exporter.run(loaded->project, s, {}, no);
    CHECK(static_cast<bool>(r));
    if (!r) { std::printf("    export error: %s\n", r.error().fullText().c_str()); return; }

    // The MP4, decoded independently of the renderer.
    auto probe = media::MediaProbe::probe(s.outputPath);
    CHECK(probe && probe->hasVideo() && probe->hasAudio());
    CHECK(probe && std::abs(probe->duration.toSeconds() - 3.5) < 0.1);        // final duration = timeline
    const auto before = frameAt(s.outputPath, 0.25);                           // gap left by the move: black
    CHECK(!before.isNull() && meanLuma(before, 0, 0, before.width, before.height) < 8);
    const auto mid = frameAt(s.outputPath, 2.0);                               // grey bars + text
    const auto late = frameAt(s.outputPath, 3.0);                              // grey bars, text gone
    CHECK(!mid.isNull() && !late.isNull());
    if (mid.isNull() || late.isNull()) return;
    CHECK(meanLuma(mid, 0, 260, mid.width, 360) > 40);                         // there is a picture...
    CHECK(meanChroma(mid, 0, 260, mid.width, 360) < 6);                        // ...without colour
    // White text, centred 250 px (720p) = 125 px (360p) above the middle.
    int brightMid = 0, brightLate = 0;
    for (int y = 180 - 125 - 25; y < 180 - 125 + 25; ++y)
        for (int x = 200; x < 440; ++x) {
            brightMid += lumaAt(mid, x, y) > 235;
            brightLate += lumaAt(late, x, y) > 235;
        }
    CHECK(brightMid > 300 && brightMid > brightLate * 3);
    int brightCentre = 0;                                                      // and not at the default (centre) position
    for (int y = 155; y < 205; ++y)
        for (int x = 200; x < 440; ++x) brightCentre += lumaAt(mid, x, y) > 235 && lumaAt(late, x, y) <= 235;
    CHECK(brightCentre < brightMid / 4);
    const auto fade = frameAt(s.outputPath, 0.75);                             // halfway through the fade-in
    CHECK(!fade.isNull() && meanLuma(fade, 0, 260, fade.width, 360) < meanLuma(late, 0, 260, late.width, 360) * 0.75);
    // Audio: half the tone's own peak while it plays; silence before it starts.
    auto srcPeak = playback::measurePeak(tone.path, TimePoint::fromSeconds(0.5), Duration::fromSeconds(1));
    auto outPeak = playback::measurePeak(s.outputPath, TimePoint::fromSeconds(1.0), Duration::fromSeconds(1));
    auto silent = playback::measurePeak(s.outputPath, TimePoint::fromSeconds(0.0), Duration::fromSeconds(0.4));
    CHECK(srcPeak && outPeak && silent);
    if (srcPeak && outPeak && silent) {
        std::printf("    tone peak %.3f, exported %.3f, before %.4f\n", *srcPeak, *outPeak, *silent);
        CHECK(std::abs(*outPeak - *srcPeak * 0.5f) < *srcPeak * 0.08f);
        CHECK(*silent < 0.01f);
    }
}

TEST_CASE("export regressions (spec 80.31): tracks, mute/solo, speed, audio fade, empty timeline, missing media") {
    auto g = gpu();
    Exporter exporter(g);
    std::atomic<bool> no{false};
    auto peak = [](const std::string& path, double from, double length) {
        auto p = playback::measurePeak(path, TimePoint::fromSeconds(from), Duration::fromSeconds(length));
        return p ? *p : -1.0f;
    };

    // Two video tracks: bars (no audio) scaled into the top-left corner of V2 over the countdown on V1.
    {
        Project p = countdownProject();
        Document doc;
        doc.reset(p);
        auto info = media::MediaProbe::probe(mediaPath("bars_360p25_noaudio.mp4"));
        CHECK(static_cast<bool>(info));
        MediaItem bars;
        bars.id = doc.allocateMediaId();
        bars.path = mediaPath("bars_360p25_noaudio.mp4");
        bars.displayName = "bars";
        if (info) bars.info = *info;
        doc.project().media.push_back(bars);
        ops::Placement place;
        place.videoTrack = doc.project().sequence.videoTracksBottomUp().back()->id;   // V2
        auto clips = ops::clipsForMedia(doc, bars, place);
        CHECK(clips.size() == 1);
        for (auto& c : clips) {
            c.duration = Duration::fromSeconds(2);
            c.transform.scaleX = c.transform.scaleY = 0.25f;
            c.transform.positionX = -480.0f;
            c.transform.positionY = -270.0f;
        }
        doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, std::move(clips))));
        ExportSettings s = smallSettings("two_tracks.mp4");
        s.rangeEnd = TimePoint::fromSeconds(2);
        auto r = exporter.run(doc.project(), s, {}, no);
        CHECK(static_cast<bool>(r));
        const auto both = frameAt(s.outputPath, 1.0);
        // Hide V2: the corner shows the countdown again.
        for (auto& t : doc.project().sequence.tracks)
            if (t.id == place.videoTrack) t.muted = true;
        ExportSettings s2 = smallSettings("v2_hidden.mp4");
        s2.rangeEnd = s.rangeEnd;
        CHECK(static_cast<bool>(exporter.run(doc.project(), s2, {}, no)));
        const auto hidden = frameAt(s2.outputPath, 1.0);
        CHECK(!both.isNull() && !hidden.isNull());
        if (!both.isNull() && !hidden.isNull()) {
            double diffCorner = 0, diffCentre = 0;   // corner = PiP area (top-left quarter of a quarter)
            for (int y = 10; y < 80; ++y)
                for (int x = 10; x < 150; ++x) diffCorner += std::abs(lumaAt(both, x, y) - lumaAt(hidden, x, y));
            for (int y = 200; y < 270; ++y)
                for (int x = 300; x < 440; ++x) diffCentre += std::abs(lumaAt(both, x, y) - lumaAt(hidden, x, y));
            CHECK(diffCorner > diffCentre * 4 + 1000);   // only the picture-in-picture area differs
        }
    }

    // Muted and solo audio tracks; audio fade-in; 2x speed.
    {
        Document doc;
        doc.reset(countdownProject());
        auto& seq = doc.project().sequence;
        ExportSettings s = smallSettings("audio_base.mp4");
        s.rangeEnd = TimePoint::fromSeconds(3);
        CHECK(static_cast<bool>(exporter.run(doc.project(), s, {}, no)));
        const float base = peak(s.outputPath, 1.0, 1.0);
        CHECK(base > 0.01f);

        TrackId a1 = TrackId::Invalid;
        for (const auto& c : seq.clips) if (c.kind == ClipKind::Audio) a1 = c.track;
        for (auto& t : seq.tracks) if (t.id == a1) t.muted = true;
        ExportSettings m = smallSettings("audio_muted.mp4");
        m.rangeEnd = s.rangeEnd;
        CHECK(static_cast<bool>(exporter.run(doc.project(), m, {}, no)));
        CHECK(peak(m.outputPath, 0.2, 2.5) < 0.001f);                      // muted track: silent

        for (auto& t : seq.tracks) {
            if (t.id == a1) t.muted = false;
            else if (t.kind == TrackKind::Audio) t.solo = true;                // solo the empty A2
        }
        ExportSettings so = smallSettings("audio_solo_other.mp4");
        so.rangeEnd = s.rangeEnd;
        CHECK(static_cast<bool>(exporter.run(doc.project(), so, {}, no)));
        CHECK(peak(so.outputPath, 0.2, 2.5) < 0.001f);                     // another track soloed: A1 silent
        for (auto& t : seq.tracks) t.solo = (t.id == a1);
        ExportSettings sa = smallSettings("audio_solo_self.mp4");
        sa.rangeEnd = s.rangeEnd;
        CHECK(static_cast<bool>(exporter.run(doc.project(), sa, {}, no)));
        CHECK(std::abs(peak(sa.outputPath, 1.0, 1.0) - base) < base * 0.1f);   // A1 soloed: unchanged
        for (auto& t : seq.tracks) t.solo = false;

        for (auto& c : seq.clips) if (c.kind == ClipKind::Audio) c.audio.fadeIn = Duration::fromSeconds(2);
        ExportSettings f = smallSettings("audio_fade.mp4");
        f.rangeEnd = s.rangeEnd;
        CHECK(static_cast<bool>(exporter.run(doc.project(), f, {}, no)));
        const float early = peak(f.outputPath, 0.0, 0.3), later = peak(f.outputPath, 2.2, 0.5);
        CHECK(early < base * 0.3f && later > base * 0.8f);                  // rises over the fade

        // 2x speed: the 6 s clip lasts 3 s and the export follows the timeline.
        for (auto& c : seq.clips) c.audio.fadeIn = Duration{};
        std::vector<ClipId> ids;
        for (const auto& c : seq.clips) ids.push_back(c.id);
        doc.push(std::make_unique<SequenceEditCommand>(doc, "Speed", ops::setSpeed(doc, ids, Rational{2, 1})));
        CHECK(seq.duration() == Duration::fromSeconds(3));
        ExportSettings sp = smallSettings("speed2x.mp4");
        auto r = exporter.run(doc.project(), sp, {}, no);
        CHECK(static_cast<bool>(r));
        auto probe = media::MediaProbe::probe(sp.outputPath);
        CHECK(probe && std::abs(probe->duration.toSeconds() - 3.0) < 0.1);
        // Frame at 1 s of the 2x export = frame at 2 s of the source.
        const auto fast = frameAt(sp.outputPath, 1.0), src2 = frameAt(mediaPath("countdown_720p30.mp4"), 2.0), src1 = frameAt(mediaPath("countdown_720p30.mp4"), 1.0);
        CHECK(!fast.isNull() && !src1.isNull() && !src2.isNull());
        if (!fast.isNull() && !src1.isNull() && !src2.isNull()) {
            auto diff = [&](const media::Image& a, const media::Image& b) {
                double d = 0;
                for (int y = 0; y < 360; y += 4)
                    for (int x = 0; x < 640; x += 4) d += std::abs(lumaAt(a, x, y) - lumaAt(b, x, y));
                return d;
            };
            CHECK(diff(fast, src2) < diff(fast, src1) * 0.5);
        }
    }

    // Empty timeline: refused with a reason, nothing written.
    {
        Project empty;
        ExportSettings s = smallSettings("empty.mp4");
        std::filesystem::remove(s.outputPath);
        auto r = exporter.run(empty, s, {}, no);
        CHECK(!r && !std::filesystem::exists(s.outputPath));
    }

    // Missing media (deleted after the project was opened): refused, never rendered as black.
    {
        std::filesystem::create_directories(kOutDir);
        const std::string copy = kOutDir + "/vanishing.mp4";
        std::filesystem::copy_file(mediaPath("countdown_720p30.mp4"), copy, std::filesystem::copy_options::overwrite_existing);
        Project p = countdownProject();
        p.media[0].path = copy;
        std::filesystem::remove(copy);
        ExportSettings s = smallSettings("offline.mp4");
        std::filesystem::remove(s.outputPath);
        auto r = exporter.run(p, s, {}, no);
        CHECK(!r);
        if (!r) CHECK(r.error().message.find("missing") != std::string::npos);
        CHECK(!std::filesystem::exists(s.outputPath));
    }
}

TEST_CASE("export: decoders of clips that have left the screen are closed during long renders") {
    auto g = gpu();
    Project p = countdownProject();
    // Replace the timeline with 10 back-to-back 9-frame clips of the same file (different In points).
    const Clip* v = nullptr;
    for (const auto& c : p.sequence.clips) if (c.kind == ClipKind::Video) v = &c;
    CHECK(v != nullptr);
    if (!v) return;
    Clip base = *v;
    std::vector<Clip> clips;
    for (int i = 0; i < 10; ++i) {
        Clip c = base;
        c.id = ClipId{static_cast<std::uint64_t>(900 + i)};
        c.link = ClipId::Invalid;
        c.start = TimePoint{9 * i * frameDuration(fps::f30).ticks};
        c.duration = Duration{9 * frameDuration(fps::f30).ticks};
        c.sourceIn = TimePoint{(10 * i % 150) * frameDuration(fps::f30).ticks};
        clips.push_back(c);
    }
    p.sequence.clips = clips;
    ExportSettings s = smallSettings("decoder_release.mp4");
    s.includeAudio = false;
    Exporter exporter(g);
    std::atomic<bool> no{false};
    auto r = exporter.run(p, s, {}, no);
    CHECK(static_cast<bool>(r));
    if (r) CHECK(r->frames == 90);
    // Idle decoders close after 15 frames, so at most 3 of the 10 clips are ever open together.
    CHECK(exporter.peakOpenClips() >= 1 && exporter.peakOpenClips() <= 3);
}
