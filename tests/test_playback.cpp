#include "TestMain.h"

#include "lumaforge/audio/AudioOutput.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/media/MediaProbe.h"
#include "lumaforge/playback/PlaybackEngine.h"
#include "lumaforge/playback/SourceProgram.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace lf;

namespace {
std::string mediaPath(const char* name) { return std::string(LF_TEST_MEDIA_DIR) + "/" + name; }
}  // namespace

TEST_CASE("audio output: create, start with silence, stop, destroy") {
    auto out = audio::AudioOutput::create({});
    if (!out) { std::printf("     (no audio device: %s)\n", out.error().fullText().c_str()); return; }
    CHECK((*out)->sampleRate() == 48000);
    int calls = 0;
    CHECK((*out)->start([&](float* buf, int frames) { std::fill(buf, buf + frames * 2, 0.0f); ++calls; }).ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    (*out)->stop();
    CHECK(calls > 0);
    out->reset();
}

TEST_CASE("playback engine: lifecycle without a surface (WARP)") {
    auto gpu = hw::GpuDevice::create({.forceWarp = true});
    CHECK(gpu.ok());
    if (!gpu) return;
    auto engine = playback::PlaybackEngine::create(*gpu, false);
    CHECK(engine.ok());
    if (!engine) return;
    auto info = media::MediaProbe::probe(mediaPath("countdown_720p30.mp4"));
    CHECK(info.ok());
    if (!info) return;
    auto program = std::make_shared<playback::SourceProgram>(*info, 1920, 1080, fps::f30);
    (*engine)->setProgram(program);
    (*engine)->play();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK((*engine)->isPlaying());
    CHECK((*engine)->position().toSeconds() > 0.2);
    (*engine)->pause();
    (*engine)->seek(TimePoint::fromSeconds(1.0));
    CHECK(std::abs((*engine)->position().toSeconds() - 1.0) < 1e-6);
    (*engine)->stepFrames(1);
    CHECK((*engine)->position().ticks == timeOfFrame(31, fps::f30).ticks);
    engine->reset();
    std::printf("     (engine destroyed cleanly)\n");
}
