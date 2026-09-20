#include "TestMain.h"

#include "lumaforge/media/MediaProbe.h"
#include "lumaforge/media/Thumbnailer.h"
#include "lumaforge/media/Waveform.h"

#include <cmath>
#include <string>

using namespace lf;
using namespace lf::media;

namespace {
std::string mediaPath(const char* name) { return std::string(LF_TEST_MEDIA_DIR) + "/" + name; }
}  // namespace

TEST_CASE("probe: H.264 + AAC MP4 reports full metadata") {
    auto r = MediaProbe::probe(mediaPath("countdown_720p30.mp4"));
    CHECK(r.ok());
    if (!r) return;
    const MediaInfo& m = r.value();
    CHECK(m.type == MediaType::Video);
    CHECK(m.fileName == "countdown_720p30.mp4");
    CHECK(m.fileSize > 1'000'000);
    CHECK(std::abs(m.duration.toSeconds() - 6.0) < 0.15);
    CHECK(m.video.has_value());
    CHECK(m.audio.has_value());
    if (m.video) {
        CHECK(m.video->codecName == "h264");
        CHECK(m.video->width == 1280);
        CHECK(m.video->height == 720);
        CHECK(m.video->frameRate == Rational(30, 1));
        CHECK(m.video->pixelFormat == "yuv420p");
        CHECK(m.video->frameCount >= 170 && m.video->frameCount <= 181);
    }
    if (m.audio) {
        CHECK(m.audio->codecName == "aac");
        CHECK(m.audio->channels == 1);
        CHECK(m.audio->sampleRate == 48000);
    }
}

TEST_CASE("probe: video without audio, 25 fps") {
    auto r = MediaProbe::probe(mediaPath("bars_360p25_noaudio.mp4"));
    CHECK(r.ok());
    if (!r) return;
    CHECK(r->type == MediaType::Video);
    CHECK(r->hasVideo());
    CHECK(!r->hasAudio());
    CHECK(r->video->frameRate == fps::f25);
    CHECK(r->video->width == 640);
}

TEST_CASE("probe: WAV is audio-only") {
    auto r = MediaProbe::probe(mediaPath("tone_44k.wav"));
    CHECK(r.ok());
    if (!r) return;
    CHECK(r->type == MediaType::Audio);
    CHECK(!r->hasVideo());
    CHECK(r->audio->sampleRate == 44100);
    CHECK(r->audio->codecName == "pcm_s16le");
    CHECK(std::abs(r->duration.toSeconds() - 3.0) < 0.05);
}

TEST_CASE("probe: PNG and JPEG are images with zero duration") {
    auto png = MediaProbe::probe(mediaPath("blue_800x600.png"));
    CHECK(png.ok());
    if (png) {
        CHECK(png->type == MediaType::Image);
        CHECK(png->duration.isZero());
        CHECK(png->video->width == 800);
        CHECK(png->video->height == 600);
        CHECK(png->video->frameCount == 1);
    }
    auto jpg = MediaProbe::probe(mediaPath("red_64.jpg"));
    CHECK(jpg.ok());
    if (jpg) {
        CHECK(jpg->type == MediaType::Image);
        CHECK(jpg->video->width == 64);
    }
}

TEST_CASE("probe: corrupt and missing files fail with user-facing messages") {
    auto bad = MediaProbe::probe(mediaPath("corrupt.mp4"));
    CHECK(!bad.ok());
    if (!bad) {
        CHECK(!bad.error().message.empty());
        CHECK(bad.error().message.find("avformat") == std::string::npos);   // technical detail stays in .detail
        CHECK(!bad.error().detail.empty());
    }
    auto missing = MediaProbe::probe(mediaPath("does_not_exist.mp4"));
    CHECK(!missing.ok());
    if (!missing) CHECK(missing.error().message.find("not be found") != std::string::npos);
}

TEST_CASE("looksLikeMedia screens by extension only") {
    CHECK(MediaProbe::looksLikeMedia("C:/x/clip.MP4"));
    CHECK(MediaProbe::looksLikeMedia("song.flac"));
    CHECK(MediaProbe::looksLikeMedia("photo.jpeg"));
    CHECK(!MediaProbe::looksLikeMedia("readme.txt"));
    CHECK(!MediaProbe::looksLikeMedia("noext"));
}

TEST_CASE("thumbnail: video frame scaled within bounds, aspect preserved") {
    Thumbnailer::Options opt;
    opt.maxWidth = 160;
    opt.maxHeight = 90;
    auto r = Thumbnailer::render(mediaPath("countdown_720p30.mp4"), opt);
    CHECK(r.ok());
    if (!r) return;
    CHECK(r->width == 160);
    CHECK(r->height == 90);
    CHECK(r->bgra.size() == static_cast<std::size_t>(160 * 90 * 4));
    // testsrc2 is colourful: the frame must not be uniformly black.
    std::uint64_t sum = 0;
    for (auto b : r->bgra) sum += b;
    CHECK(sum > static_cast<std::uint64_t>(r->bgra.size()) * 10);
}

TEST_CASE("thumbnail: image and tall/short sources respect bounds") {
    Thumbnailer::Options opt;
    opt.maxWidth = 100;
    opt.maxHeight = 100;
    auto png = Thumbnailer::render(mediaPath("blue_800x600.png"), opt);
    CHECK(png.ok());
    if (png) {
        CHECK(png->width == 100);
        CHECK(png->height == 75);
        // Solid blue 0x3366cc -> BGRA (0xCC, 0x66, 0x33, ...)
        CHECK(png->bgra[0] > 0xB0 && png->bgra[1] > 0x50 && png->bgra[1] < 0x80 && png->bgra[2] < 0x50);
    }
    auto tiny = Thumbnailer::render(mediaPath("red_64.jpg"), opt);
    CHECK(tiny.ok());
    if (tiny) CHECK(tiny->width == 64 && tiny->height == 64);   // never upscaled
}

TEST_CASE("thumbnail: audio-only file is rejected cleanly") {
    auto r = Thumbnailer::render(mediaPath("tone_44k.wav"));
    CHECK(!r.ok());
}

TEST_CASE("waveform: peaks cover the tone file and are silent outside it") {
    auto w = lf::media::analyseWaveform(std::string(LF_TEST_MEDIA_DIR) + "/tone_44k.wav");
    CHECK(w.ok());
    if (!w) { std::printf("     %s\n", w.error().fullText().c_str()); return; }
    CHECK(!w->empty());
    const double seconds = w->duration().toSeconds();
    CHECK(seconds > 0.5);
    // The tone peaks at -18 dBFS (0.125 -> 32/255).
    const int peak = w->peakBetween(lf::TimePoint::fromSeconds(0.1), lf::TimePoint::fromSeconds(0.2));
    CHECK(peak > 24 && peak < 45);
    CHECK(w->peakBetween(lf::TimePoint::fromSeconds(seconds + 1), lf::TimePoint::fromSeconds(seconds + 2)) == 0);
}
