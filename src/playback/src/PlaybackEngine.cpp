#include "lumaforge/playback/PlaybackEngine.h"

#include "lumaforge/audio/AudioOutput.h"
#include "lumaforge/core/Log.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/playback/AudioMixer.h"
#include "lumaforge/playback/VideoReader.h"

#include <windows.h>

#include <algorithm>
#include <chrono>

namespace lf::playback {

namespace {
constexpr const char* kLog = "playback";
using Clock = std::chrono::steady_clock;
constexpr int kMixerRate = 48000;
constexpr int kMixerChannels = 2;
}  // namespace

const char* previewQualityName(PreviewQuality q) noexcept {
    switch (q) {
        case PreviewQuality::Auto: return "Auto";
        case PreviewQuality::P360: return "360p";
        case PreviewQuality::P480: return "480p";
        case PreviewQuality::P720: return "720p";
        case PreviewQuality::Full: return "Full";
    }
    return "?";
}

Result<std::unique_ptr<PlaybackEngine>> PlaybackEngine::create(std::shared_ptr<hw::GpuDevice> gpu, bool allowHardwareDecode) {
    if (!gpu) return Error{"Playback needs a Direct3D 11 device (hardware or WARP)."};
    std::unique_ptr<PlaybackEngine> e(new PlaybackEngine());
    e->m_gpu = std::move(gpu);
    e->m_allowHardware = allowHardwareDecode;
    auto comp = render::Compositor::create(e->m_gpu);
    if (!comp) return comp.error();
    e->m_compositor = std::move(comp.value());

    e->m_mixer = std::make_unique<AudioMixer>(kMixerRate, kMixerChannels);
    audio::AudioOutput::Options ao;
    ao.sampleRate = kMixerRate;
    ao.channels = kMixerChannels;
    if (auto out = audio::AudioOutput::create(ao)) {
        e->m_audio = std::move(out.value());
        AudioMixer* mixer = e->m_mixer.get();
        if (auto s = e->m_audio->start([mixer](float* buf, int frames) { mixer->render(buf, frames); }); !s) {
            LF_LOG_WARN(kLog, "audio output failed to start: {}", s.error().fullText());
            e->m_audio.reset();
        }
    } else {
        LF_LOG_WARN(kLog, "no audio output: {} (playback continues silently)", out.error().fullText());
    }

    e->m_stats.renderer = e->m_gpu->description();
    e->m_stats.decoder = "-";
    e->m_thread = std::thread([raw = e.get()] { raw->threadMain(); });
    LF_LOG_INFO(kLog, "playback engine started ({}, hardware decode {})", e->m_gpu->description(), allowHardwareDecode ? "allowed" : "disabled");
    return e;
}

PlaybackEngine::~PlaybackEngine() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    wake();
    if (m_thread.joinable()) m_thread.join();
    if (m_audio) m_audio->stop();
    m_readers.clear();
    m_target.reset();
    m_surface.reset();
    LF_LOG_INFO(kLog, "playback engine stopped");
}

void PlaybackEngine::setCallbacks(Callbacks callbacks) {
    std::lock_guard lock(m_mutex);
    m_callbacks = std::move(callbacks);
}

// ---------------------------------------------------------------- surface

void PlaybackEngine::attachSurface(std::unique_ptr<render::PreviewRenderer> surface) {
    std::lock_guard lock(m_mutex);
    m_surface = std::move(surface);
    if (m_surface) { m_surfaceW = m_surface->width(); m_surfaceH = m_surface->height(); }
    m_redraw = true;
    wake();
}

void PlaybackEngine::detachSurface() {
    // Must not race the render thread: hand the destruction to it.
    std::unique_lock lock(m_mutex);
    m_surfaceDirty = true;
    m_surfaceW = m_surfaceH = 0;
    wake();
    m_cv.wait(lock, [&] { return !m_surface || m_stop; });
}

void PlaybackEngine::resizeSurface(int width, int height) {
    std::lock_guard lock(m_mutex);
    m_surfaceW = width;
    m_surfaceH = height;
    m_surfaceDirty = true;
    m_redraw = true;
    wake();
}

void PlaybackEngine::requestRedraw() {
    std::lock_guard lock(m_mutex);
    m_redraw = true;
    m_retryCount = 0;
    wake();
}

// ---------------------------------------------------------------- transport

void PlaybackEngine::setProgram(std::shared_ptr<const Program> program, TimePoint keepPosition) {
    {
        std::lock_guard lock(m_mutex);
        m_program = std::move(program);
        const Ticks dur = m_program ? m_program->duration().ticks : 0;
        Ticks pos = keepPosition.ticks >= 0 ? keepPosition.ticks : 0;
        pos = std::clamp<Ticks>(pos, 0, std::max<Ticks>(0, dur));
        m_position = pos;
        if (m_playing.load()) {
            m_playOrigin = TimePoint{pos};
            m_playWall = Clock::now();
        }
        m_redraw = true;
        m_retryCount = 0;
        m_stats.decoder = "-";
    }
    m_mixer->setProgram(m_program);
    m_mixer->setTime(position());
    LF_LOG_DEBUG(kLog, "program set: {} ({:.3f} s, {}x{}), position {:.3f} s", m_program ? "yes" : "none",
                 m_program ? m_program->duration().toSeconds() : 0.0, m_program ? m_program->frameWidth() : 0,
                 m_program ? m_program->frameHeight() : 0, position().toSeconds());
    wake();
}

std::shared_ptr<const Program> PlaybackEngine::program() const {
    std::lock_guard lock(m_mutex);
    return m_program;
}

void PlaybackEngine::play() {
    {
        std::lock_guard lock(m_mutex);
        if (!m_program || m_playing.load()) return;
        Ticks pos = m_position.load();
        if (pos >= m_program->duration().ticks) pos = 0;   // replay from the start when at the end
        m_position = pos;
        m_playOrigin = TimePoint{pos};
        m_stopReturn = TimePoint{pos};
        m_playWall = Clock::now();
        m_playing = true;
        m_fpsCounter = 0;
        m_fpsWindowStart = Clock::now();
    }
    m_mixer->setTime(position());
    m_mixer->setPlaying(true);
    LF_LOG_DEBUG(kLog, "play from {:.3f} s", position().toSeconds());
    if (m_callbacks.playingChanged) m_callbacks.playingChanged(true);
    wake();
}

void PlaybackEngine::pause() {
    bool was = false;
    {
        std::lock_guard lock(m_mutex);
        was = m_playing.exchange(false);
        if (was) m_position = std::clamp<Ticks>(clockNow().ticks, 0, m_program ? m_program->duration().ticks : 0);
        m_redraw = true;
        m_retryCount = 0;
    }
    m_mixer->setPlaying(false);
    if (was) LF_LOG_DEBUG(kLog, "pause at {:.3f} s", position().toSeconds());
    if (was && m_callbacks.playingChanged) m_callbacks.playingChanged(false);
    notifyPosition(position(), true);
    wake();
}

void PlaybackEngine::togglePlay() {
    if (m_playing.load()) pause();
    else play();
}

void PlaybackEngine::stop() {
    if (m_playing.load()) {
        pause();
        seek(m_stopReturn);
    } else {
        seek(TimePoint::zero());
    }
}

void PlaybackEngine::seek(TimePoint t, bool scrubbing) {
    {
        std::lock_guard lock(m_mutex);
        const Ticks dur = m_program ? m_program->duration().ticks : 0;
        const Ticks pos = std::clamp<Ticks>(t.ticks, 0, dur);
        m_position = pos;
        m_scrubbing = scrubbing;
        if (m_playing.load()) {
            m_playOrigin = TimePoint{pos};
            m_playWall = Clock::now();
        }
        m_redraw = true;
        m_retryCount = 0;
    }
    m_mixer->setTime(position());
    notifyPosition(position(), true);
    wake();
}

void PlaybackEngine::stepFrames(int frames) {
    if (m_playing.load()) pause();
    std::shared_ptr<const Program> prog = program();
    if (!prog) return;
    const Ticks fd = frameDuration(prog->frameRate()).ticks;
    const Ticks cur = m_position.load();
    // Step from the frame boundary containing the current position.
    const Ticks base = (cur / fd) * fd;
    seek(TimePoint{base + frames * fd});
}

void PlaybackEngine::setQuality(PreviewQuality q) {
    m_quality = q;
    requestRedraw();
}

void PlaybackEngine::setMuted(bool muted) { m_mixer->setMuted(muted); }

PlaybackStats PlaybackEngine::stats() const {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

TimePoint PlaybackEngine::clockNow() const {
    // Steady clock is the master; the audio mixer is re-aligned on play/seek.
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_playWall).count();
    return TimePoint{m_playOrigin.ticks + static_cast<Ticks>(elapsed * (kFlicksPerSecond / 1'000'000'000.0))};
}

void PlaybackEngine::notifyPosition(TimePoint t, bool force) {
    const auto now = Clock::now();
    if (!force && now - m_lastPositionNotify < std::chrono::milliseconds(33)) return;
    m_lastPositionNotify = now;
    if (m_callbacks.positionChanged) m_callbacks.positionChanged(t);
}

// ---------------------------------------------------------------- rendering

VideoReader& PlaybackEngine::readerFor(const VideoLayerRequest& req) {
    const std::string key = req.path + '#' + std::to_string(req.layerKey);
    auto it = m_readers.find(key);
    if (it == m_readers.end()) {
        VideoReader::Options o;
        o.gpu = m_gpu;
        o.pool = m_compositor->texturePoolPtr();
        o.allowHardware = m_allowHardware;
        o.isImage = req.isImage;
        ReaderEntry e;
        e.path = req.path;
        e.reader = std::make_unique<VideoReader>(req.path, o);
        it = m_readers.emplace(key, std::move(e)).first;
    }
    it->second.lastUsed = Clock::now();
    return *it->second.reader;
}

void PlaybackEngine::pruneReaders() {
    if (!m_program) { m_readers.clear(); return; }
    // Drop readers whose media left the program, and readers idle for a while
    // (each one holds a decoder, a prefetch queue and GPU textures). A short
    // grace period keeps scrubbing back and forth across a cut cheap.
    constexpr auto kIdle = std::chrono::seconds(4);
    constexpr std::size_t kMaxReaders = 8;
    const auto paths = m_program->mediaPaths();
    const auto now = Clock::now();
    for (auto it = m_readers.begin(); it != m_readers.end();) {
        const bool gone = std::find(paths.begin(), paths.end(), it->second.path) == paths.end();
        const bool idle = now - it->second.lastUsed > kIdle;
        if (gone || (idle && m_readers.size() > 2)) it = m_readers.erase(it);
        else ++it;
    }
    while (m_readers.size() > kMaxReaders) {
        auto oldest = std::min_element(m_readers.begin(), m_readers.end(), [](const auto& a, const auto& b) { return a.second.lastUsed < b.second.lastUsed; });
        m_readers.erase(oldest);
    }
}

std::shared_ptr<render::FrameTexture> PlaybackEngine::textTextureFor(const TextParams& text, int frameW, int frameH) {
    const std::uint64_t key = render::TextRasterizer::hash(text, frameW, frameH);
    if (auto it = m_textCache.find(key); it != m_textCache.end()) {
        it->second.lastUse = ++m_textUseCounter;
        return it->second.texture;
    }
    if (m_textUnavailable) return nullptr;
    if (!m_text) {
        auto created = render::TextRasterizer::create();
        if (!created) {
            LF_LOG_ERROR(kLog, "text rendering unavailable: {}", created.error().fullText());
            m_textUnavailable = true;
            return nullptr;
        }
        m_text = std::move(created.value());
    }
    auto bitmap = m_text->render(text, frameW, frameH);
    if (!bitmap) {
        LF_LOG_WARN(kLog, "text render failed: {}", bitmap.error().fullText());
        return nullptr;
    }
    auto tex = m_compositor->texturePoolPtr()->acquireBgra(bitmap->width, bitmap->height, true);
    if (!tex) {
        LF_LOG_WARN(kLog, "text texture failed: {}", tex.error().fullText());
        return nullptr;
    }
    if (auto s = (*tex)->fillBgra(bitmap->bgra.data(), bitmap->width * 4, true); !s) {
        LF_LOG_WARN(kLog, "text upload failed: {}", s.error().fullText());
        return nullptr;
    }
    // Bounded: a title change per slider tick must not accumulate 8 MB textures.
    constexpr std::size_t kMaxTextTextures = 6;
    while (m_textCache.size() >= kMaxTextTextures) {
        auto oldest = std::min_element(m_textCache.begin(), m_textCache.end(), [](const auto& a, const auto& b) { return a.second.lastUse < b.second.lastUse; });
        m_textCache.erase(oldest);
    }
    m_textCache[key] = TextEntry{*tex, ++m_textUseCounter};
    return *tex;
}

void PlaybackEngine::ensureTarget(int frameW, int frameH) {
    int w = frameW, h = frameH;
    auto scaleToHeight = [&](int targetH) {
        if (frameH > targetH) { w = std::max(2, static_cast<int>(static_cast<std::int64_t>(frameW) * targetH / frameH)) & ~1; h = targetH; }
    };
    switch (m_quality.load()) {
        case PreviewQuality::P360: scaleToHeight(360); break;
        case PreviewQuality::P480: scaleToHeight(480); break;
        case PreviewQuality::P720: scaleToHeight(720); break;
        case PreviewQuality::Full: break;
        case PreviewQuality::Auto: {
            // Never render more pixels than the surface can show; on top of
            // that, cap at 720p while playing on this class of hardware.
            const int surfaceH = m_surface ? m_surface->height() : 720;
            const int surfaceW = m_surface ? m_surface->width() : 1280;
            int capH = std::min(frameH, std::max(180, surfaceH));
            if (frameW * surfaceH > surfaceW * frameH) capH = std::min(capH, std::max(180, static_cast<int>(static_cast<std::int64_t>(surfaceW) * frameH / frameW)));
            if (m_playing.load()) capH = std::min(capH, 720);
            scaleToHeight(capH);
            break;
        }
    }
    if (m_target && m_target->width() == w && m_target->height() == h) return;
    auto t = m_compositor->createTarget(w, h);
    if (t) m_target = std::move(t.value());
}

void PlaybackEngine::renderOnce(TimePoint t, bool vsync) {
    if (!m_surface) return;
    if (!m_program) {
        render::PreviewScene scene;
        m_surface->render(scene);
        return;
    }
    const int fw = m_program->frameWidth(), fh = m_program->frameHeight();
    ensureTarget(fw, fh);
    if (!m_target) return;

    // At the very end, keep showing the last frame instead of an empty program.
    const Ticks dur = m_program->duration().ticks;
    if (dur > 0 && t.ticks >= dur) t = TimePoint{dur - 1};
    m_layerRequests.clear();
    m_program->videoLayersAt(t, m_layerRequests);
    m_layers.clear();
    bool starved = false;
    std::string decoderName = "-";
    for (const auto& req : m_layerRequests) {
        if (req.path.empty()) {
            // Generated layer: text rendered at frame size, cached by content.
            if (!req.text) continue;
            auto tex = textTextureFor(*req.text, fw, fh);
            if (!tex) continue;
            render::Layer layer;
            layer.texture = std::move(tex);
            layer.transform = req.transform;
            layer.crop = req.crop;
            layer.color = req.color;
            layer.effects = req.effects;
            layer.fitToFrame = true;
            m_layers.push_back(std::move(layer));
            continue;
        }
        VideoReader& reader = readerFor(req);
        bool exact = false;
        auto tex = reader.frameAt(req.sourceTime, exact);
        if (reader.hasError()) continue;
        if (!exact) starved = true;
        if (!tex) continue;
        if (!req.isImage) decoderName = reader.isHardware() ? "D3D11VA" : "Software";
        render::Layer layer;
        layer.texture = std::move(tex);
        layer.transform = req.transform;
        layer.crop = req.crop;
        layer.color = req.color;
        layer.effects = req.effects;
        layer.fitToFrame = req.fitToFrame;
        m_layers.push_back(std::move(layer));
    }
    // Look-ahead hint: readers decode towards where playback is going.
    if (m_playing.load()) {
        for (const auto& req : m_layerRequests) if (!req.path.empty()) readerFor(req).hint(req.sourceTime);
    }

    if (auto s = m_compositor->compose(*m_target, fw, fh, m_layers, render::Rgba{0, 0, 0, 1}); !s) {
        LF_LOG_WARN(kLog, "compose failed: {}", s.error().fullText());
        return;
    }
    const render::IntRect dst = render::fitFrame(m_surface->width(), m_surface->height(), fw, fh);
    const render::Rgba surround{0.086f, 0.094f, 0.106f, 1.0f};
    if (auto s = m_compositor->blit(*m_target, m_surface->backBuffer(), m_surface->width(), m_surface->height(), dst, surround); !s) {
        LF_LOG_WARN(kLog, "blit failed: {}", s.error().fullText());
        return;
    }
    if (auto s = m_surface->present(vsync); !s) {
        LF_LOG_WARN(kLog, "present failed: {}", s.error().fullText());
        if (m_callbacks.error) m_callbacks.error(s.error().message);
        return;
    }
    m_stats.presentedFrames++;
    m_stats.starved = starved;
    m_stats.decoder = decoderName;
    m_stats.renderWidth = m_target->width();
    m_stats.renderHeight = m_target->height();
    {
        std::lock_guard lock(m_mutex);
        m_retryRender = starved;
    }
}

void PlaybackEngine::threadMain() {
    SetThreadDescription(GetCurrentThread(), L"lf-playback");
    m_lastStats = Clock::now();
    m_fpsWindowStart = Clock::now();

    while (true) {
        bool redraw = false;
        bool surfaceDirty = false;
        int sw = 0, sh = 0;
        TimePoint renderTime;
        bool playing = false;
        {
            std::unique_lock lock(m_mutex);
            if (m_playing.load()) {
                // Sleep until the next frame boundary is due.
                const Ticks fd = m_program ? frameDuration(m_program->frameRate()).ticks : frameDuration(fps::f30).ticks;
                const Ticks now = clockNow().ticks;
                const Ticks nextFrame = ((now / fd) + 1) * fd;
                const auto waitNs = std::chrono::nanoseconds(static_cast<long long>((nextFrame - now) * (1'000'000'000.0 / kFlicksPerSecond)));
                m_cv.wait_for(lock, waitNs, [&] { return m_stop || m_redraw || m_surfaceDirty; });
            } else if (m_retryRender && m_retryCount < kMaxRetries) {
                // Paused, but the last frame was incomplete (a reader was still
                // decoding towards it): poll briefly until it arrives.
                m_cv.wait_for(lock, std::chrono::milliseconds(12), [&] { return m_stop || m_redraw || m_surfaceDirty || m_playing.load(); });
                m_redraw = true;
                ++m_retryCount;
            } else {
                // play() flips m_playing and wakes us: it must be part of the predicate,
                // otherwise a wake-up with nothing else pending goes straight back to sleep.
                m_cv.wait(lock, [&] { return m_stop || m_redraw || m_surfaceDirty || m_playing.load(); });
            }
            if (m_stop) break;
            redraw = m_redraw;
            m_redraw = false;
            surfaceDirty = m_surfaceDirty;
            m_surfaceDirty = false;
            sw = m_surfaceW;
            sh = m_surfaceH;
            playing = m_playing.load();
            if (playing) {
                TimePoint now = clockNow();
                const Ticks dur = m_program ? m_program->duration().ticks : 0;
                if (now.ticks >= dur) {
                    now = TimePoint{dur};
                    m_position = dur;
                    m_playing = false;
                    playing = false;
                    lock.unlock();
                    m_mixer->setPlaying(false);
                    LF_LOG_DEBUG(kLog, "reached end at {:.3f} s", TimePoint{dur}.toSeconds());
                    if (m_callbacks.playingChanged) m_callbacks.playingChanged(false);
                    notifyPosition(TimePoint{dur}, true);
                    lock.lock();
                    redraw = true;
                } else {
                    m_position = now.ticks;
                }
            }
            renderTime = TimePoint{m_position.load()};
        }

        if (surfaceDirty) {
            if (sw <= 0 || sh <= 0) {
                std::lock_guard lock(m_mutex);
                m_surface.reset();
                m_cv.notify_all();
                continue;
            }
            if (m_surface) {
                if (auto s = m_surface->resize(sw, sh); !s) LF_LOG_WARN(kLog, "surface resize failed: {}", s.error().fullText());
            }
        }

        if (playing || redraw) {
            // Frame pacing: when we are more than a frame late, count a drop
            // (we render the current time rather than every missed frame).
            if (playing && m_program) {
                const Ticks fd = frameDuration(m_program->frameRate()).ticks;
                static thread_local Ticks lastRendered = -1;
                if (lastRendered >= 0 && renderTime.ticks - lastRendered >= 2 * fd) m_stats.droppedFrames += static_cast<std::uint64_t>((renderTime.ticks - lastRendered) / fd - 1);
                lastRendered = renderTime.ticks;
            }
            pruneReaders();
            renderOnce(renderTime, playing && !m_scrubbing);
            if (playing) {
                ++m_fpsCounter;
                notifyPosition(renderTime, false);
            }
        }

        const auto now = Clock::now();
        if (now - m_lastStats >= std::chrono::milliseconds(500)) {
            const double seconds = std::chrono::duration<double>(now - m_fpsWindowStart).count();
            PlaybackStats snapshot;
            {
                std::lock_guard lock(m_mutex);
                m_stats.previewFps = (playing && seconds > 0) ? m_fpsCounter / seconds : 0.0;
                snapshot = m_stats;
            }
            m_fpsCounter = 0;
            m_fpsWindowStart = now;
            m_lastStats = now;
            if (m_callbacks.statsChanged) m_callbacks.statsChanged(snapshot);
        }
    }
    // Release GPU objects on the thread that used them.
    m_readers.clear();
    m_textCache.clear();
    m_text.reset();
    m_target.reset();
    m_surface.reset();
}

}  // namespace lf::playback
