#include "lumaforge/export/Exporter.h"

#include "Muxer.h"
#include "lumaforge/export/FastCut.h"
#include "lumaforge/core/Log.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/media/MediaProbe.h"
#include "lumaforge/media/VideoDecoder.h"
#include "lumaforge/playback/AudioMixer.h"
#include "lumaforge/playback/LayerFactory.h"
#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/rendering/Compositor.h"
#include "lumaforge/project/Subtitles.h"

#include <fstream>

#include <chrono>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace lf::exporting {

namespace {
constexpr const char* kLog = "export";

std::filesystem::path toPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string fromPath(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

// Exact frames for one clip instance: decodes forward to the frame that
// covers each requested time, seeking only when going backwards or far ahead.
class ClipFrames {
public:
    ClipFrames(std::string path, bool image, std::shared_ptr<hw::GpuDevice> gpu, std::shared_ptr<render::FrameTexturePool> pool)
        : m_path(std::move(path)), m_image(image), m_gpu(std::move(gpu)), m_pool(std::move(pool)) {}

    Result<std::shared_ptr<render::FrameTexture>> frameAt(TimePoint t) {
        if (!m_decoder) {
            media::VideoDecoder::Options o;
            o.gpu = m_gpu;
            o.allowHardware = m_gpu != nullptr && !m_image;
            auto dec = media::VideoDecoder::open(m_path, o);
            if (!dec) return dec.error();
            m_decoder = std::move(dec.value());
        }
        if (m_image) {
            if (!m_texture) {
                auto f = m_decoder->next();
                if (!f) return f.error();
                if (!f->has_value()) return Error{"The image could not be decoded."};
                auto tex = m_pool->acquireFor(**f);
                if (!tex) return tex.error();
                m_texture = *tex;
            }
            return m_texture;
        }
        const bool backwards = m_lastAsked.ticks >= 0 && t < m_lastAsked;
        const bool farAhead = m_lastAsked.ticks >= 0 && t.ticks - m_lastAsked.ticks > 2 * kFlicksPerSecond;
        if (m_lastAsked.ticks < 0 || backwards || farAhead) {
            if (auto s = m_decoder->seek(t); !s) return s.error();
            m_current.reset();
            m_pending.reset();
            m_texture.reset();
        }
        m_lastAsked = t;
        if (m_pending && m_pending->pts() <= t) {
            m_current = std::move(m_pending);
            m_pending.reset();
            m_texture.reset();
        }
        // Advance until the current frame covers t (or the stream ends: hold the last frame).
        while (!(m_current && m_current->pts() <= t && t < m_current->pts() + m_current->duration())) {
            auto f = m_decoder->next();
            if (!f) return f.error();
            if (!f->has_value()) break;
            if ((*f)->pts() > t && m_current) {
                m_pending = std::move(**f);
                break;
            }
            m_current = std::move(**f);
            m_texture.reset();
        }
        if (!m_current) return Error{"No picture could be decoded at this time."};
        if (!m_texture) {
            auto tex = m_pool->acquireFor(*m_current);
            if (!tex) return tex.error();
            m_texture = *tex;
        }
        return m_texture;
    }

    [[nodiscard]] bool isHardware() const noexcept { return m_decoder && m_decoder->isHardware(); }

private:
    std::string m_path;
    bool m_image;
    std::shared_ptr<hw::GpuDevice> m_gpu;
    std::shared_ptr<render::FrameTexturePool> m_pool;
    std::unique_ptr<media::VideoDecoder> m_decoder;
    std::optional<media::VideoFrame> m_current;
    std::optional<media::VideoFrame> m_pending;
    std::shared_ptr<render::FrameTexture> m_texture;
    TimePoint m_lastAsked{-1};
};
}  // namespace

struct Exporter::Impl {
    std::shared_ptr<hw::GpuDevice> gpu;
    std::unique_ptr<render::Compositor> compositor;
    std::unique_ptr<playback::LayerFactory> layers;
    std::unordered_map<std::uint64_t, std::unique_ptr<ClipFrames>> clips;   // by layer key (clip id)
    std::unordered_map<std::uint64_t, std::int64_t> lastUsed;              // layer key -> last frame index drawn
    std::size_t peakOpen = 0;

    // Long timelines: a decoder (and its GPU surfaces) per clip ever drawn would
    // exhaust video memory on small GPUs (the GT 730 target lost its D3D11
    // device ~30 clips into a reel). Clips not drawn for kIdleFrames are closed; a clip that
    // returns re-opens and seeks, which is cheap by comparison.
    static constexpr std::int64_t kIdleFrames = 15;
    void prune(std::int64_t frame) {
        for (auto it = clips.begin(); it != clips.end();) {
            const auto used = lastUsed.find(it->first);
            if (used == lastUsed.end() || frame - used->second > kIdleFrames) {
                lastUsed.erase(it->first);
                it = clips.erase(it);
            } else {
                ++it;
            }
        }
    }

    Status ensureDevice() {
        if (compositor) return Status::success();
        if (!gpu) {
            auto created = hw::GpuDevice::create({.allowDebugLayer = false, .allowWarp = true, .forceWarp = true});
            if (!created) return created.error();
            gpu = std::move(created.value());
        }
        auto comp = render::Compositor::create(gpu);
        if (!comp) return comp.error();
        compositor = std::move(comp.value());
        layers = std::make_unique<playback::LayerFactory>(compositor->texturePoolPtr());
        return Status::success();
    }

    // Composites the program at t into `target`.
    Status compose(const playback::Program& program, TimePoint t, render::RenderTarget& target, int frameW, int frameH, std::int64_t frame = 0) {
        std::vector<playback::VideoLayerRequest> requests;
        program.videoLayersAt(t, requests);
        std::vector<render::Layer> built;
        built.reserve(requests.size());
        for (const auto& req : requests) {
            if (req.path.empty()) {
                render::Layer layer;
                if (layers->generated(req, frameW, frameH, layer)) built.push_back(std::move(layer));
                continue;
            }
            auto& source = clips[req.layerKey];
            if (!source) source = std::make_unique<ClipFrames>(req.path, req.isImage, gpu, compositor->texturePoolPtr());
            lastUsed[req.layerKey] = frame;
            peakOpen = std::max(peakOpen, clips.size());
            auto tex = source->frameAt(req.sourceTime);
            if (!tex) return Error{"A clip could not be decoded for export: " + req.path, tex.error().fullText()};
            built.push_back(playback::LayerFactory::media(req, std::move(tex.value())));
        }
        return compositor->compose(target, frameW, frameH, built, render::Rgba{0, 0, 0, 1});
    }
};

Exporter::Exporter(std::shared_ptr<hw::GpuDevice> gpu) : m_impl(std::make_unique<Impl>()) { m_impl->gpu = std::move(gpu); }
Exporter::~Exporter() = default;

std::size_t Exporter::peakOpenClips() const noexcept { return m_impl->peakOpen; }

Result<media::Image> Exporter::renderFrame(const project::Project& snapshot, TimePoint t, int width, int height) {
    Impl& d = *m_impl;
    if (auto s = d.ensureDevice(); !s) return s.error();
    const int w = width > 0 ? width : snapshot.settings.width;
    const int h = height > 0 ? height : snapshot.settings.height;
    playback::SequenceProgram program(snapshot);
    auto target = d.compositor->createTarget(w, h);
    if (!target) return target.error();
    if (auto s = d.compose(program, t, **target, snapshot.settings.width, snapshot.settings.height); !s) return s.error();
    media::Image img;
    img.width = w;
    img.height = h;
    if (auto s = d.compositor->readback(**target, img.bgra); !s) return s.error();
    d.clips.clear();
    return img;
}

Result<ExportResult> Exporter::run(const project::Project& original, const ExportSettings& settings, const ProgressFn& progress, const std::atomic<bool>& cancel) {
    // Subtitles not burnt in: their tracks are left out of the picture.
    project::Project snapshot = original;
    if (!settings.burnSubtitles) for (auto& t : snapshot.sequence.tracks) if (t.subtitles) t.muted = true;
    Impl& d = *m_impl;
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    if (const auto problems = validate(settings, snapshot); !problems.empty()) return Error{problems.front()};
    // Missing media is never silently rendered as black: files may have gone away since the project was opened.
    for (auto& m : snapshot.media) {
        std::error_code ec;
        if (!m.offline && !std::filesystem::exists(toPath(m.path), ec)) m.offline = true;
    }
    if (const auto missing = offlineMediaInRange(settings, snapshot); !missing.empty())
        return Error{"Media is missing: " + missing.front() + (missing.size() > 1 ? " (and " + std::to_string(missing.size() - 1) + " more)" : std::string{}) + ". Relink it before exporting."};
    if (settings.losslessFastCut) {
        // Stream copy: no decoding, compositing or encoding at all.
        const FastCutCheck check = checkFastCut(snapshot, settings);
        ExportProgress p;
        p.totalFrames = frameCount(settings, snapshot);
        auto done = fastCut(check, settings, [&](double f) {
            p.frame = static_cast<std::int64_t>(f * p.totalFrames);
            p.elapsedSeconds = std::chrono::duration<double>(Clock::now() - started).count();
            p.framesPerSecond = p.elapsedSeconds > 0 ? p.frame / p.elapsedSeconds : 0.0;
            if (progress) progress(p);
        }, cancel);
        if (!done) return done.error();
        auto probe = media::MediaProbe::probe(settings.outputPath);
        if (!probe || !probe->hasVideo()) return Error{"The exported file could not be read back."};
        ExportResult result;
        result.path = settings.outputPath;
        result.frames = p.totalFrames;
        result.videoEncoder = "copy";
        result.audioEncoder = probe->hasAudio() ? "copy" : "";
        result.width = probe->video->width;
        result.height = probe->video->height;
        result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
        std::error_code ec;
        result.bytes = static_cast<std::uint64_t>(std::filesystem::file_size(toPath(settings.outputPath), ec));
        result.note = "Lossless copy of the source: no re-encoding (starts at the keyframe at or before the In point).";
        return result;
    }
    if (auto s = d.ensureDevice(); !s) return s.error();
    d.clips.clear();
    d.lastUsed.clear();
    d.peakOpen = 0;

    int w = 0, h = 0;
    outputSize(settings, snapshot, w, h);
    const Rational fps = outputFrameRate(settings, snapshot);   // the timeline is evaluated at each output frame time
    const TimeRange range = exportRange(settings, snapshot);
    const std::int64_t total = frameCount(settings, snapshot);
    auto program = std::make_shared<playback::SequenceProgram>(snapshot);
    const bool audio = settings.includeAudio && program->hasAudio();

    // Write to a partial file; rename when complete.
    const auto finalPath = toPath(settings.outputPath);
    auto partial = finalPath;
    partial.replace_extension(std::u8string(u8".partial") + finalPath.extension().u8string());
    std::error_code ec;
    std::filesystem::remove(partial, ec);

    Muxer::Spec spec;
    spec.path = fromPath(partial);
    spec.settings = settings;
    spec.width = w;
    spec.height = h;
    spec.frameRate = fps;
    spec.audio = audio;
    spec.audioChannels = settings.audioChannels;
    spec.title = settings.title.empty() ? snapshot.settings.name : settings.title;
    spec.comment = settings.comment.empty() ? std::string("Made with LumaForge") : settings.comment;
    LF_LOG_INFO(kLog, "export start: {} | {}x{} @ {}/{} | {} frames ({:.2f} s from {:.2f} s) | audio {}", settings.outputPath, w, h, fps.num, fps.den, total,
                range.duration.toSeconds(), range.start.toSeconds(), audio ? "yes" : "no");
    auto muxer = Muxer::open(spec);
    std::string fallbackNote;
    if (!muxer && !settings.encoder.empty() && settings.encoder != defaultEncoder(settings.codec)) {
        // A hardware / OS encoder that will not start: fall back to the CPU encoder.
        LF_LOG_WARN(kLog, "encoder {} failed ({}); falling back to {}", settings.encoder, muxer.error().fullText(), defaultEncoder(settings.codec));
        fallbackNote = "Used " + std::string(defaultEncoder(settings.codec)) + " because " + settings.encoder + " could not start.";
        spec.settings.encoder.clear();
        std::filesystem::remove(partial, ec);
        muxer = Muxer::open(spec);
    }
    if (!muxer) {
        std::filesystem::remove(partial, ec);
        LF_LOG_ERROR(kLog, "export failed to start: {}", muxer.error().fullText());
        return muxer.error();
    }
    auto cleanup = [&](Error e) -> Result<ExportResult> {
        muxer->reset();
        d.clips.clear();
        std::filesystem::remove(partial, ec);
        LF_LOG_WARN(kLog, "export stopped: {}", e.fullText());
        return e;
    };

    auto target = d.compositor->createTarget(w, h);
    if (!target) return cleanup(target.error());

    playback::AudioMixer mixer(settings.audioSampleRate, settings.audioChannels);
    if (audio) {
        mixer.setProgram(program);
        mixer.setTime(range.start);
        mixer.setPlaying(true);
    }
    std::vector<std::uint8_t> bgra;
    std::vector<float> samples;
    std::int64_t samplesWritten = 0;
    const double sampleRate = settings.audioSampleRate;

    ExportProgress p;
    p.totalFrames = total;
    auto lastReport = Clock::now() - std::chrono::seconds(1);
    for (std::int64_t i = 0; i < total; ++i) {
        if (cancel.load()) return cleanup(Error{"Export cancelled."});
        const TimePoint t{range.start.ticks + timeOfFrame(i, fps).ticks};
        if (auto s = d.compose(*program, t, **target, snapshot.settings.width, snapshot.settings.height, i); !s) return cleanup(s.error());
        d.prune(i);
        if (auto s = d.compositor->readback(**target, bgra); !s) return cleanup(s.error());
        if (auto s = (*muxer)->writeVideo(bgra.data(), w * 4, i); !s) return cleanup(s.error());
        if (audio) {
            // Audio up to the end of this frame, sample-exact against the frame clock.
            const double endSeconds = (timeOfFrame(i + 1, fps).ticks) / static_cast<double>(kFlicksPerSecond);
            const std::int64_t want = std::min<std::int64_t>(static_cast<std::int64_t>(endSeconds * sampleRate + 0.5),
                                                             static_cast<std::int64_t>(range.duration.toSeconds() * sampleRate + 0.5));
            const int n = static_cast<int>(std::max<std::int64_t>(0, want - samplesWritten));
            if (n > 0) {
                samples.assign(static_cast<std::size_t>(n) * settings.audioChannels, 0.0f);
                mixer.render(samples.data(), n);
                if (auto s = (*muxer)->writeAudio(samples.data(), n); !s) return cleanup(s.error());
                samplesWritten += n;
            }
        }
        p.frame = i + 1;
        const auto now = Clock::now();
        p.elapsedSeconds = std::chrono::duration<double>(now - started).count();
        p.framesPerSecond = p.elapsedSeconds > 0 ? p.frame / p.elapsedSeconds : 0.0;
        if (progress && (now - lastReport > std::chrono::milliseconds(100) || p.frame == total)) {
            progress(p);
            lastReport = now;
        }
    }
    if (auto s = (*muxer)->finish(); !s) return cleanup(s.error());
    ExportResult result;
    result.videoEncoder = (*muxer)->videoEncoderName();
    result.audioEncoder = (*muxer)->audioEncoderName();
    result.note = fallbackNote;
    muxer->reset();
    d.clips.clear();

    // Replace any previous file, then verify what was written can be read back.
    std::filesystem::remove(finalPath, ec);
    std::filesystem::rename(partial, finalPath, ec);
    if (ec) {
        std::filesystem::remove(partial, ec);
        return Error{"The finished video could not be renamed into place.", ec.message()};
    }
    auto probe = media::MediaProbe::probe(settings.outputPath);
    if (!probe) return Error{"The exported file could not be read back.", probe.error().fullText()};
    if (!probe->hasVideo()) return Error{"The exported file has no video stream."};
    if (settings.subtitleSidecar) {
        const auto cues = project::subtitleCues(original.sequence, range);
        if (!cues.empty()) {
            auto srt = finalPath;
            srt.replace_extension(".srt");
            std::ofstream out(srt, std::ios::binary);
            out << project::writeSrt(cues);
            if (!out) LF_LOG_WARN(kLog, "could not write subtitles {}", fromPath(srt));
            else LF_LOG_INFO(kLog, "wrote {} subtitle cues to {}", cues.size(), fromPath(srt));
        }
    }
    result.path = settings.outputPath;
    result.frames = total;
    result.width = w;
    result.height = h;
    result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    result.bytes = static_cast<std::uint64_t>(std::filesystem::file_size(finalPath, ec));
    LF_LOG_INFO(kLog, "export done: {} | {} frames in {:.1f} s ({:.1f} fps) | {} bytes | probe {:.2f} s {}", result.path, result.frames, result.seconds,
                result.seconds > 0 ? result.frames / result.seconds : 0.0, result.bytes, probe->duration.toSeconds(), probe->hasAudio() ? "with audio" : "no audio");
    return result;
}

}  // namespace lf::exporting
