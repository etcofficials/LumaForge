#include "TestMain.h"

#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/media/AudioDecoder.h"
#include "lumaforge/media/VideoDecoder.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace lf;
using namespace lf::media;

namespace {
std::string mediaPath(const char* name) { return std::string(LF_TEST_MEDIA_DIR) + "/" + name; }

// Collect up to n frames.
std::vector<VideoFrame> pull(VideoDecoder& d, int n) {
    std::vector<VideoFrame> out;
    while (static_cast<int>(out.size()) < n) {
        auto r = d.next();
        if (!r || !r->has_value()) break;
        out.push_back(std::move(**r));
    }
    return out;
}
}  // namespace

TEST_CASE("video decoder (software): frames in order with exact 1/30 s spacing") {
    VideoDecoder::Options opt;
    opt.allowHardware = false;
    auto d = VideoDecoder::open(mediaPath("countdown_720p30.mp4"), opt);
    CHECK(d.ok());
    if (!d) return;
    CHECK(!(*d)->isHardware());
    CHECK((*d)->info().width == 1280);
    CHECK((*d)->frameDuration() == frameDuration(fps::f30));
    auto frames = pull(**d, 12);
    CHECK(frames.size() == 12);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        CHECK(frames[i].pts().ticks == timeOfFrame(static_cast<std::int64_t>(i), fps::f30).ticks);
        CHECK(frames[i].layout() == PixelLayout::Yuv420p);
        CHECK(frames[i].plane(0) != nullptr);
        CHECK(frames[i].width() == 1280 && frames[i].height() == 720);
    }
}

TEST_CASE("video decoder: seek lands before target and decodes forward to the exact frame") {
    VideoDecoder::Options opt;
    opt.allowHardware = false;
    auto d = VideoDecoder::open(mediaPath("countdown_720p30.mp4"), opt);
    CHECK(d.ok());
    if (!d) return;
    const TimePoint target = timeOfFrame(93, fps::f30);
    CHECK((*d)->seek(target).ok());
    VideoFrame hit;
    int decoded = 0;
    while (true) {
        auto r = (*d)->next();
        CHECK(r.ok());
        if (!r || !r->has_value()) break;
        ++decoded;
        if ((*r)->pts() >= target) { hit = std::move(**r); break; }
    }
    CHECK(!hit.isNull());
    CHECK(hit.pts().ticks == target.ticks);   // frame-exact, no drift
    CHECK(decoded >= 1);
    std::printf("     (decoded %d frames after keyframe seek to reach frame 93)\n", decoded);
}

TEST_CASE("video decoder: end of stream is reported cleanly") {
    VideoDecoder::Options opt;
    opt.allowHardware = false;
    auto d = VideoDecoder::open(mediaPath("bars_360p25_noaudio.mp4"), opt);
    CHECK(d.ok());
    if (!d) return;
    auto frames = pull(**d, 1000);
    CHECK(frames.size() >= 99 && frames.size() <= 101);   // 4 s @ 25 fps
    auto tail = (*d)->next();
    CHECK(tail.ok() && !tail->has_value());
}

TEST_CASE("video decoder: hardware path is used when the device supports it, else software") {
    auto gpu = hw::GpuDevice::create({});
    CHECK(gpu.ok());
    if (!gpu) return;
    VideoDecoder::Options opt;
    opt.gpu = *gpu;
    opt.allowHardware = true;
    auto d = VideoDecoder::open(mediaPath("countdown_720p30.mp4"), opt);
    CHECK(d.ok());
    if (!d) return;
    const bool expectHw = (*gpu)->videoDevice() != nullptr && !(*gpu)->isWarp();
    std::printf("     (device: %s, decoder: %s)\n", (*gpu)->description().c_str(), (*d)->isHardware() ? "D3D11VA" : "software");
    // A hardware decoder owns a fixed pool of surfaces, so consumers keep only
    // a small window of GPU frames alive (the renderer copies them out). Hold 3.
    std::vector<VideoFrame> frames;
    int count = 0;
    TimePoint lastPts;
    for (int n = 0; n < 30; ++n) {
        auto r = (*d)->next();
        if (!r) { std::printf("     next() error at frame %d: %s\n", n, r.error().fullText().c_str()); break; }
        if (!r->has_value()) { std::printf("     EOF at frame %d\n", n); break; }
        lastPts = (*r)->pts();
        if ((*d)->isHardware()) {
            CHECK((*r)->isGpu());
            CHECK((*r)->d3d11Texture() != nullptr);
        }
        frames.push_back(std::move(**r));
        if (frames.size() > 3) frames.erase(frames.begin());
        ++count;
    }
    CHECK(count == 30);
    if ((*d)->isHardware()) CHECK(expectHw);
    if (count == 30) CHECK(lastPts.ticks == timeOfFrame(29, fps::f30).ticks);
    // Seeking works identically on the hardware path.
    std::printf("     seeking\n");
    CHECK((*d)->seek(timeOfFrame(60, fps::f30)).ok());
    std::printf("     pulling after seek\n");
    auto after = pull(**d, 1);
    CHECK(after.size() == 1);
    std::printf("     releasing frames\n");
    frames.clear();
    after.clear();
    std::printf("     closing decoder\n");
    d->reset();
    std::printf("     done\n");
}

TEST_CASE("audio decoder: resamples 44.1k mono WAV to 48k stereo with correct length") {
    AudioDecoder::Options opt;
    opt.sampleRate = 48000;
    opt.channels = 2;
    auto d = AudioDecoder::open(mediaPath("tone_44k.wav"), opt);
    CHECK(d.ok());
    if (!d) return;
    CHECK((*d)->info().sampleRate == 44100);
    std::vector<float> buf(4096 * 2);
    std::int64_t total = 0;
    float peak = 0;
    while (true) {
        auto r = (*d)->read(buf.data(), 4096);
        CHECK(r.ok());
        if (!r || *r == 0) break;
        for (int i = 0; i < *r * 2; ++i) peak = std::max(peak, std::abs(buf[static_cast<std::size_t>(i)]));
        total += *r;
    }
    std::printf("     (total %lld frames, peak %.4f)\n", static_cast<long long>(total), static_cast<double>(peak));
    CHECK(std::abs(total - 3 * 48000) < 200);
    CHECK(peak > 0.10f && peak < 0.15f);   // ffmpeg's sine source generates at amplitude 1/8
}

TEST_CASE("audio decoder: sample-accurate seek and position tracking") {
    AudioDecoder::Options opt;
    auto d = AudioDecoder::open(mediaPath("countdown_720p30.mp4"), opt);
    CHECK(d.ok());
    if (!d) return;
    const TimePoint t = TimePoint::fromSeconds(2.5);
    CHECK((*d)->seek(t).ok());
    CHECK((*d)->position().ticks == t.ticks);
    std::vector<float> buf(4800 * 2);
    auto r = (*d)->read(buf.data(), 4800);
    CHECK(r.ok() && *r == 4800);
    CHECK(std::abs((*d)->position().toSeconds() - 2.6) < 1e-6);
    // The 440 Hz sine must be present (non-silent) after the seek.
    float energy = 0;
    for (float v : buf) energy += v * v;
    CHECK(energy > 30.0f);   // 4800 stereo samples of a 0.125-amplitude sine ~= 75
}

TEST_CASE("decoders reject files without the requested stream") {
    VideoDecoder::Options v;
    v.allowHardware = false;
    CHECK(!VideoDecoder::open(mediaPath("tone_44k.wav"), v).ok());
    CHECK(!AudioDecoder::open(mediaPath("bars_360p25_noaudio.mp4"), {}).ok());
    CHECK(!VideoDecoder::open(mediaPath("corrupt.mp4"), v).ok());
}
