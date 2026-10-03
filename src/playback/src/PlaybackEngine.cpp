#include "lumaforge/playback/PlaybackEngine.h"

#include "lumaforge/audio/AudioOutput.h"
#include "lumaforge/core/Log.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/playback/AudioMixer.h"
#include "lumaforge/playback/VideoReader.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>

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
        case PreviewQuality::Full: return "Full";
        case PreviewQuality::Half: return "1/2";
        case PreviewQuality::Quarter: return "1/4";
        case PreviewQuality::Eighth: return "1/8";
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

void PlaybackEngine::play(double rate) {
    if (rate == 0.0) { pause(); return; }
    bool wasPlaying = false;
    {
        std::lock_guard lock(m_mutex);
        if (!m_program) return;
        wasPlaying = m_playing.load();
        Ticks pos = wasPlaying ? std::clamp<Ticks>(clockNow().ticks, 0, m_program->duration().ticks) : m_position.load();
        // Replay from the start when at the end (or from the end when reversing at the start).
        if (!wasPlaying && rate > 0 && pos >= m_program->duration().ticks) pos = 0;
        if (!wasPlaying && rate < 0 && pos <= 0) pos = m_program->duration().ticks;
        m_position = pos;
        m_playOrigin = TimePoint{pos};
        if (!wasPlaying) m_stopReturn = TimePoint{pos};
        m_playWall = Clock::now();
        m_rate = rate;
        m_playing = true;
        m_fpsCounter = 0;
        m_fpsWindowStart = Clock::now();
        m_retryCount = 0;
    }
    m_mixer->setTime(position());
    // Shuttle speeds are silent: pitch-shifted 8x audio is noise, not information.
    m_mixer->setPlaying(rate == 1.0);
    LF_LOG_DEBUG(kLog, "play from {:.3f} s at {}x", position().toSeconds(), rate);
    if (!wasPlaying && m_callbacks.playingChanged) m_callbacks.playingChanged(true);
    wake();
}

void PlaybackEngine::pause() {
    bool was = false;
    {
        std::lock_guard lock(m_mutex);
        if (m_playing.load()) m_position = std::clamp<Ticks>(clockNow().ticks, 0, m_program ? m_program->duration().ticks : 0);
        was = m_playing.exchange(false);
        m_rate = 1.0;
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

void PlaybackEngine::setLoop(bool enabled, TimePoint in, TimePoint out) {
    std::lock_guard lock(m_mutex);
    m_loop = enabled;
    m_loopIn = in;
    m_loopOut = out;
}

void PlaybackEngine::setViewZoom(double zoom, int panX, int panY) {
    {
        std::lock_guard lock(m_mutex);
        m_viewZoom = zoom;
        m_panX = panX;
        m_panY = panY;
    }
    requestRedraw();
}

PlaybackStats PlaybackEngine::stats() const {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

TimePoint PlaybackEngine::clockNow() const {
    // Steady clock is the master; the audio mixer is re-aligned on play/seek.
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_playWall).count();
    return TimePoint{m_playOrigin.ticks + static_cast<Ticks>(elapsed * m_rate.load() * (kFlicksPerSecond / 1'000'000'000.0))};
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

std::shared_ptr<render::FrameTexture> PlaybackEngine::solidTextureFor(const ColorRgba& color) {
    auto channel = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    const std::uint32_t bgra = channel(color.b) | (channel(color.g) << 8) | (channel(color.r) << 16) | (channel(color.a) << 24);
    if (auto it = m_solidCache.find(bgra); it != m_solidCache.end()) return it->second;
    constexpr int kSize = 16;
    auto tex = m_compositor->texturePoolPtr()->acquireBgra(kSize, kSize, true);
    if (!tex) return nullptr;
    std::vector<std::uint32_t> pixels(kSize * kSize, bgra);
    if (!(*tex)->fillBgra(reinterpret_cast<const std::uint8_t*>(pixels.data()), kSize * 4, false)) return nullptr;
    if (m_solidCache.size() > 8) m_solidCache.clear();
    m_solidCache[bgra] = *tex;
    return *tex;
}

void PlaybackEngine::ensureTarget(int frameW, int frameH) {
    int w = frameW, h = frameH;
    auto scaleToHeight = [&](int targetH) {
        if (frameH > targetH) { w = std::max(2, static_cast<int>(static_cast<std::int64_t>(frameW) * targetH / frameH)) & ~1; h = targetH; }
    };
    switch (m_quality.load()) {
        case PreviewQuality::Full: break;
        case PreviewQuality::Half: scaleToHeight(std::max(90, frameH / 2)); break;
        case PreviewQuality::Quarter: scaleToHeight(std::max(90, frameH / 4)); break;
        case PreviewQuality::Eighth: scaleToHeight(std::max(90, frameH / 8)); break;
        case PreviewQuality::Auto: {
            // Never render more pixels than the surface shows; on top of that,
            // cap at 720p while playing on this class of hardware.
            int capH = frameH;
            if (m_viewZoom > 0.0) {
                capH = std::min(frameH, std::max(180, static_cast<int>(frameH * m_viewZoom)));
            } else {
                const int surfaceH = m_surface ? m_surface->height() : 720;
                const int surfaceW = m_surface ? m_surface->width() : 1280;
                capH = std::min(frameH, std::max(180, surfaceH));
                if (frameW * surfaceH > surfaceW * frameH) capH = std::min(capH, std::max(180, static_cast<int>(static_cast<std::int64_t>(surfaceW) * frameH / frameW)));
            }
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
        if (req.path.empty() && req.solid) {
            // Generated colour layer (dip transitions): a tiny texture stretched over the frame.
            auto tex = solidTextureFor(*req.solid);
            if (!tex) continue;
            render::Layer layer;
            layer.texture = std::move(tex);
            layer.transform = req.transform;
            layer.transform.scaleX *= std::max(1.0f, static_cast<float>(fw) / static_cast<float>(fh));
            layer.transform.scaleY *= std::max(1.0f, static_cast<float>(fh) / static_cast<float>(fw));
            layer.crop = req.crop;
            layer.mask = req.mask;
            layer.color = req.color;
            layer.effects = req.effects;
            layer.fitToFrame = true;
            m_layers.push_back(std::move(layer));
            continue;
        }
        if (req.path.empty() && req.adjustment) {
            render::Layer layer;
            layer.adjustment = true;
            layer.transform = req.transform;
            layer.crop = req.crop;
            layer.mask = req.mask;
            layer.color = req.color;
            layer.effects = req.effects;
            m_layers.push_back(std::move(layer));
            continue;
        }
        if (req.path.empty()) {
            // Generated layer: text rendered at frame size, cached by content.
            if (!req.text) continue;
            auto tex = textTextureFor(*req.text, fw, fh);
            if (!tex) continue;
            render::Layer layer;
            layer.texture = std::move(tex);
            layer.transform = req.transform;
            layer.crop = req.crop;
            layer.mask = req.mask;
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
        layer.mask = req.mask;
        layer.color = req.color;
        layer.effects = req.effects;
        layer.fitToFrame = req.fitToFrame;
        m_layers.push_back(std::move(layer));
    }
    // Look-ahead hint: readers decode towards where playback is going.
    if (m_playing.load()) {
        for (const auto& req : m_layerRequests) if (!req.path.empty()) readerFor(req).hint(req.sourceTime);
        // Pre-roll: clips starting within the next half second (in the direction of
        // travel) get their reader opened and positioned now, so a cut does not show
        // a cold decoder's first-frame latency.
        const double rate = m_rate.load();
        const Ticks ahead = static_cast<Ticks>(kFlicksPerSecond / 2 * (rate < 0 ? -1.0 : 1.0) * std::max(1.0, std::abs(rate)));
        const Ticks future = std::clamp<Ticks>(t.ticks + ahead, 0, std::max<Ticks>(0, dur - 1));
        m_prerollRequests.clear();
        m_program->videoLayersAt(TimePoint{future}, m_prerollRequests);
        for (const auto& req : m_prerollRequests) {
            if (req.path.empty()) continue;
            const bool current = std::any_of(m_layerRequests.begin(), m_layerRequests.end(), [&](const VideoLayerRequest& r) { return r.layerKey == req.layerKey; });
            if (!current) readerFor(req).hint(req.sourceTime);
        }
    }

    if (auto s = m_compositor->compose(*m_target, fw, fh, m_layers, render::Rgba{0, 0, 0, 1}); !s) {
        LF_LOG_WARN(kLog, "compose failed: {}", s.error().fullText());
        return;
    }
    render::IntRect dst = render::fitFrame(m_surface->width(), m_surface->height(), fw, fh);
    if (m_viewZoom > 0.0) {
        // Magnified: frame pixels map to screen pixels by the zoom, centred plus pan;
        // the rasteriser clips whatever falls outside the surface.
        const int w = std::max(2, static_cast<int>(fw * m_viewZoom));
        const int h = std::max(2, static_cast<int>(fh * m_viewZoom));
        dst = render::IntRect{(m_surface->width() - w) / 2 + m_panX, (m_surface->height() - h) / 2 + m_panY, w, h};
    }
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
                // Sleep until the next frame boundary is due. Shuttle speeds render
                // at most one frame per project frame period of wall time.
                const Ticks fd = m_program ? frameDuration(m_program->frameRate()).ticks : frameDuration(fps::f30).ticks;
                const double rate = m_rate.load();
                long long waitTicks = fd;
                if (rate == 1.0) {
                    const Ticks now = clockNow().ticks;
                    waitTicks = ((now / fd) + 1) * fd - now;
                }
                const auto waitNs = std::chrono::nanoseconds(static_cast<long long>(waitTicks * (1'000'000'000.0 / kFlicksPerSecond)));
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
                const double rate = m_rate.load();
                // Loop range: In/Out when both are set and valid, else the whole program.
                Ticks rangeStart = 0, rangeEnd = dur;
                if (m_loop && m_loopIn.ticks >= 0 && m_loopOut.ticks > m_loopIn.ticks) {
                    rangeStart = std::clamp<Ticks>(m_loopIn.ticks, 0, dur);
                    rangeEnd = std::clamp<Ticks>(m_loopOut.ticks, 0, dur);
                }
                const bool pastEnd = rate > 0 && now.ticks >= (m_loop ? rangeEnd : dur);
                const bool pastStart = rate < 0 && now.ticks <= (m_loop ? rangeStart : 0);
                if ((pastEnd || pastStart) && m_loop && rangeEnd > rangeStart) {
                    const Ticks restart = rate > 0 ? rangeStart : rangeEnd - 1;
                    m_playOrigin = TimePoint{restart};
                    m_playWall = Clock::now();
                    m_position = restart;
                    lock.unlock();
                    m_mixer->setTime(TimePoint{restart});
                    lock.lock();
                } else if (pastEnd || pastStart) {
                    const Ticks stopAt = pastEnd ? dur : 0;
                    m_position = stopAt;
                    m_playing = false;
                    m_rate = 1.0;
                    playing = false;
                    lock.unlock();
                    m_mixer->setPlaying(false);
                    LF_LOG_DEBUG(kLog, "reached {} at {:.3f} s", pastEnd ? "end" : "start", TimePoint{stopAt}.toSeconds());
                    if (m_callbacks.playingChanged) m_callbacks.playingChanged(false);
                    notifyPosition(TimePoint{stopAt}, true);
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
            // Only normal-speed playback can drop frames; shuttle skips by design.
            static thread_local Ticks lastRendered = -1;
            if (playing && m_program && m_rate.load() == 1.0) {
                const Ticks fd = frameDuration(m_program->frameRate()).ticks;
                const Ticks gap = renderTime.ticks - lastRendered;
                if (lastRendered >= 0 && gap >= 2 * fd && gap < 4 * kFlicksPerSecond) m_stats.droppedFrames += static_cast<std::uint64_t>(gap / fd - 1);
                lastRendered = renderTime.ticks;
            } else {
                lastRendered = -1;
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
    m_solidCache.clear();
    m_text.reset();
    m_target.reset();
    m_surface.reset();
}

}  // namespace lf::playback
