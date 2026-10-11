// V0.4 phase 13: audio processing (EQ, compressor, gate, limiter, pan), the
// mixer's track buses, master stage and meters, and reversed / frozen clips.
#include "TestMain.h"

#include "lumaforge/media/MediaProbe.h"
#include "lumaforge/playback/AudioDsp.h"
#include "lumaforge/playback/AudioMixer.h"
#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"

#include <cmath>
#include <filesystem>
#include <vector>

using namespace lf;
using namespace lf::project;
using namespace lf::playback;

namespace {
constexpr double kPi = 3.14159265358979323846;
TimePoint sec(double s) { return TimePoint::fromSeconds(s); }

std::vector<float> sine(double freq, float amplitude, int frames, int rate = 48000) {
    std::vector<float> v(static_cast<std::size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) v[2 * i] = v[2 * i + 1] = amplitude * static_cast<float>(std::sin(2 * kPi * freq * i / rate));
    return v;
}
float rms(const std::vector<float>& v, int from = 0) {
    double s = 0;
    int n = 0;
    for (std::size_t i = static_cast<std::size_t>(from) * 2; i < v.size(); ++i) { s += v[i] * v[i]; ++n; }
    return n ? static_cast<float>(std::sqrt(s / n)) : 0.0f;
}
float peak(const float* v, std::size_t n) {
    float p = 0;
    for (std::size_t i = 0; i < n; ++i) p = std::max(p, std::fabs(v[i]));
    return p;
}

// tone_44k.wav on A1 for its whole length.
Project toneProject() {
    Document doc;
    MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = std::string(LF_TEST_MEDIA_DIR) + "/tone_44k.wav";
    auto info = media::MediaProbe::probe(m.path);
    CHECK(static_cast<bool>(info));
    if (info) m.info = *info;
    doc.push(std::make_unique<ImportMediaCommand>(doc, std::vector<MediaItem>{m}));
    auto clips = ops::clipsForMedia(doc, m, ops::defaultPlacement(doc.project(), m, TimePoint::zero()));
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, std::move(clips))));
    return doc.project();
}

std::vector<float> mix(const Project& p, TimePoint at, int frames, AudioMixer::Levels* levels = nullptr) {
    AudioMixer mixer(48000, 2);
    mixer.setProgram(std::make_shared<SequenceProgram>(p));
    mixer.setTime(at);
    mixer.setPlaying(true);
    std::vector<float> out(static_cast<std::size_t>(frames) * 2);
    // Several blocks, like the device / exporter would ask.
    for (int done = 0; done < frames; done += 480) mixer.render(out.data() + static_cast<std::ptrdiff_t>(done) * 2, std::min(480, frames - done));
    if (levels) *levels = mixer.takeLevels();
    return out;
}
}  // namespace

TEST_CASE("dsp: EQ shelves affect only their band; flat EQ is exact") {
    auto low = sine(60, 0.25f, 48000);
    auto high = sine(10000, 0.25f, 48000);
    auto original = low;
    dsp::Equalizer flat;
    flat.set(48000, 0, 0, 0);
    flat.process(low.data(), 48000, 2);
    CHECK(low == original);
    dsp::Equalizer boostLow;
    boostLow.set(48000, 6.0f, 0, 0);
    boostLow.process(low.data(), 48000, 2);
    CHECK(std::abs(rms(low, 4800) / rms(original, 4800) - 2.0f) < 0.15f);   // +6 dB ~ x2
    const float highBefore = rms(high, 4800);
    dsp::Equalizer boostLow2;
    boostLow2.set(48000, 6.0f, 0, 0);
    boostLow2.process(high.data(), 48000, 2);
    CHECK(std::abs(rms(high, 4800) / highBefore - 1.0f) < 0.05f);           // 10 kHz untouched
}

TEST_CASE("dsp: compressor tames loud signals, gate mutes quiet ones, limiter holds the ceiling, pan is equal power") {
    auto loud = sine(440, 0.9f, 48000);
    dsp::Compressor comp;
    comp.set(48000, -20.0f, 4.0f);
    comp.process(loud.data(), 48000, 2);
    CHECK(comp.lastGainReductionDb() > 6.0f);
    auto quiet = sine(440, 0.001f, 48000);   // -60 dBFS
    dsp::Gate gate;
    gate.set(48000, -40.0f);
    gate.process(quiet.data(), 48000, 2);
    CHECK(rms(quiet, 24000) < 1e-5f);
    auto hot = sine(440, 1.8f, 48000);
    dsp::Limiter lim;
    lim.set(48000, -0.3f);
    lim.process(hot.data(), 48000, 2);
    CHECK(peak(hot.data(), hot.size()) <= dsp::dbToGain(-0.3f) + 1e-4f);
    std::vector<float> st{0.5f, 0.5f};
    dsp::pan(st.data(), 1, 0.0f);
    CHECK(st[0] == 0.5f && st[1] == 0.5f);
    dsp::pan(st.data(), 1, 1.0f);
    CHECK(std::abs(st[0]) < 1e-6f && st[1] > 0.5f);
}

TEST_CASE("mixer: clip pan, track volume, master gain, meters; frozen clips are silent") {
    Project p = toneProject();
    AudioMixer::Levels levels;
    const auto base = mix(p, sec(0.2), 9600, &levels);
    const float basePeak = peak(base.data(), base.size());
    CHECK(basePeak > 0.05f);
    CHECK(!levels.tracks.empty() && levels.master > 0.05f);

    Project panned = p;
    for (auto& c : panned.sequence.clips) c.audio.pan = 1.0f;   // hard right
    const auto right = mix(panned, sec(0.2), 9600);
    float leftPeak = 0, rightPeak = 0;
    for (std::size_t i = 0; i < right.size(); i += 2) { leftPeak = std::max(leftPeak, std::fabs(right[i])); rightPeak = std::max(rightPeak, std::fabs(right[i + 1])); }
    CHECK(leftPeak < 1e-4f && rightPeak > basePeak);

    Project quieter = p;
    for (auto& t : quieter.sequence.tracks) if (t.kind == TrackKind::Audio) t.volume = 0.5f;
    CHECK(std::abs(peak(mix(quieter, sec(0.2), 9600).data(), 9600 * 2) - basePeak * 0.5f) < basePeak * 0.05f);

    Project master = p;
    master.sequence.masterVolume = 0.25f;
    CHECK(std::abs(peak(mix(master, sec(0.2), 9600).data(), 9600 * 2) - basePeak * 0.25f) < basePeak * 0.05f);

    Project frozen = p;
    for (auto& c : frozen.sequence.clips) c.freeze = true;
    CHECK(peak(mix(frozen, sec(0.2), 4800).data(), 4800 * 2) == 0.0f);

    Project reversed = p;
    for (auto& c : reversed.sequence.clips) c.reverse = true;
    CHECK(peak(mix(reversed, sec(0.2), 4800).data(), 4800 * 2) > 0.05f);   // still audible, read backwards
}

TEST_CASE("reversed and frozen clips: time mapping, and every edit keeps the same picture at each time") {
    Clip c;
    c.kind = ClipKind::Video;
    c.start = sec(10);
    c.duration = Duration::fromSeconds(4);
    c.sourceIn = sec(2);   // media 2..6
    c.reverse = true;
    CHECK(c.sourceTimeAt(sec(10)).ticks == sec(6).ticks - 1);   // plays from the end
    CHECK(c.sourceTimeAt(sec(12)).ticks == sec(4).ticks - 1);
    CHECK(c.sequenceTimeAt(c.sourceTimeAt(sec(11))) == sec(11));
    for (double cut : {10.5, 12.0, 13.25}) {
        const Clip left = retimed(c, c.start, sec(cut));
        const Clip right = retimed(c, sec(cut), c.end());
        for (double t : {10.0, 10.4, 11.9, 12.0, 13.0, 13.9}) {
            const Clip& piece = t < cut ? left : right;
            CHECK(piece.sourceTimeAt(sec(t)) == c.sourceTimeAt(sec(t)));
        }
        CHECK(left.sourceIn.ticks >= c.sourceIn.ticks && right.sourceIn == c.sourceIn);
    }
    Clip frozen = c;
    frozen.reverse = false;
    frozen.freeze = true;
    CHECK(frozen.sourceTimeAt(sec(13)) == sec(2));
    CHECK(retimed(frozen, sec(11), sec(20)).sourceIn == sec(2));

    // Trimming the start of a reversed clip removes media from its end.
    Document doc;
    MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = "C:/m.mp4";
    m.info.type = media::MediaType::Video;
    m.info.duration = Duration::fromSeconds(10);
    media::VideoStreamInfo v; v.width = 640; v.height = 360; v.frameRate = fps::f30; m.info.video = v;
    doc.push(std::make_unique<ImportMediaCommand>(doc, std::vector<MediaItem>{m}));
    Clip r = c;
    r.id = doc.allocateClipId();
    r.track = doc.project().sequence.primaryTrack(TrackKind::Video)->id;
    r.media = m.id;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", ops::insertClips(doc, {r})));
    const auto& seq = doc.project().sequence;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Trim", ops::trimClip(doc, r.id, ops::Edge::Start, sec(11))));
    const Clip* t = seq.findClip(r.id);
    CHECK(t && t->start == sec(11) && t->sourceIn == sec(2) && t->sourceTimeAt(sec(12)) == r.sourceTimeAt(sec(12)));
    // Extending the end of a reversed clip reveals media before sourceIn (2 s available).
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Trim", ops::trimClip(doc, r.id, ops::Edge::End, sec(30))));
    t = seq.findClip(r.id);
    CHECK(t && t->end() == sec(16) && t->sourceIn == sec(0) && t->sourceTimeAt(sec(12)) == r.sourceTimeAt(sec(12)));
}

TEST_CASE("serializer v8: pan, audio fx, track volume/pan, master, reverse/freeze round-trip") {
    Project p = toneProject();
    for (auto& c : p.sequence.clips) {
        c.audio.pan = -0.4f;
        c.audio.fx.eqHigh = 3.0f;
        c.audio.fx.compressor = true;
        c.reverse = true;
    }
    for (auto& t : p.sequence.tracks) if (t.kind == TrackKind::Audio) { t.volume = 0.7f; t.pan = 0.2f; }
    p.sequence.masterVolume = 0.9f;
    p.sequence.masterLimiter = false;
    auto loaded = serializer::fromJson(serializer::toJson(p, ""), "");
    CHECK(static_cast<bool>(loaded));
    if (!loaded) return;
    CHECK(loaded->project.sequence.clips == p.sequence.clips);
    CHECK(loaded->project.sequence.tracks == p.sequence.tracks);
    CHECK(loaded->project.sequence.masterVolume == 0.9f && !loaded->project.sequence.masterLimiter);
}

#include "lumaforge/audio/AudioRecorder.h"
#include <chrono>
#include <thread>

TEST_CASE("voice-over recorder: records a valid WAV when a microphone exists") {
    auto rec = audio::AudioRecorder::create();
    if (!rec) { std::printf("    (no microphone: %s)\n", rec.error().fullText().c_str()); return; }
    const std::string path = LF_TEST_TMP_DIR "/lf_export_tests/recording.wav";
    std::filesystem::create_directories(LF_TEST_TMP_DIR "/lf_export_tests");
    auto started = (*rec)->start(path);
    if (!started) { std::printf("    (microphone not usable: %s)\n", started.error().fullText().c_str()); return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    auto length = (*rec)->stop();
    CHECK(static_cast<bool>(length));
    if (!length) return;
    auto info = media::MediaProbe::probe(path);
    CHECK(info && info->hasAudio() && info->audio->sampleRate == 48000 && info->audio->channels == 2);
    if (info) CHECK(std::abs(info->duration.toSeconds() - length->toSeconds()) < 0.05 && length->toSeconds() > 0.3);
}

TEST_CASE("master limiter ceiling: -1 dBFS holds a +12 dBFS overload, round-trips, defaults to -0.3") {
    Project p = toneProject();
    // tone_44k.wav peaks at -18 dBFS; +30 dB of clip gain drives the limiter about 12 dB over full scale.
    for (auto& c : p.sequence.clips) c.audio.volume = 32.0f;
    CHECK(p.sequence.masterCeilingDb == -0.3f);
    auto loud = mix(p, sec(0.2), 48000);
    CHECK(peak(loud.data(), loud.size()) <= std::pow(10.0f, -0.3f / 20.0f) + 1e-4f);
    CHECK(peak(loud.data(), loud.size()) > std::pow(10.0f, -1.0f / 20.0f));   // default ceiling lets it above -1 dBFS
    p.sequence.masterCeilingDb = -1.0f;
    auto held = mix(p, sec(0.2), 48000);
    CHECK(peak(held.data(), held.size()) <= std::pow(10.0f, -1.0f / 20.0f) + 1e-4f);
    auto loaded = serializer::fromJson(serializer::toJson(p, ""), "");
    CHECK(static_cast<bool>(loaded));
    if (loaded) CHECK(loaded->project.sequence.masterCeilingDb == -1.0f);
}
