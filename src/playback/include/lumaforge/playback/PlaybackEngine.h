#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/playback/Program.h"
#include "lumaforge/rendering/Compositor.h"
#include "lumaforge/rendering/PreviewRenderer.h"
#include "lumaforge/rendering/TextRasterizer.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace lf::hw { class GpuDevice; }
namespace lf::audio { class AudioOutput; }

namespace lf::playback {

class AudioMixer;
class VideoReader;

// Render resolution of the preview. Auto follows the viewer size (720p cap while
// playing); the fractions are of the project resolution.
enum class PreviewQuality { Auto, Full, Half, Quarter, Eighth };
[[nodiscard]] const char* previewQualityName(PreviewQuality q) noexcept;

struct PlaybackStats {
    double previewFps = 0;          // frames presented per second while playing
    std::uint64_t droppedFrames = 0; // frames skipped because rendering fell behind
    std::uint64_t presentedFrames = 0;
    std::string decoder;            // "D3D11VA" / "Software" / "-"
    std::string renderer;           // device description
    int renderWidth = 0, renderHeight = 0;
    bool starved = false;           // last frame was shown while a reader was still catching up
};

// Owns the render thread. Everything Direct3D-related for the preview
// (compositing, presenting, resizing) happens on that thread; the UI thread
// only posts commands. The audio device thread pulls from the mixer.
class PlaybackEngine {
public:
    struct Callbacks {
        std::function<void(TimePoint)> positionChanged;     // throttled while playing
        std::function<void(bool)> playingChanged;
        std::function<void(const PlaybackStats&)> statsChanged;   // ~2 Hz
        std::function<void(const std::string&)> error;
    };

    static Result<std::unique_ptr<PlaybackEngine>> create(std::shared_ptr<hw::GpuDevice> gpu, bool allowHardwareDecode);
    ~PlaybackEngine();

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    void setCallbacks(Callbacks callbacks);   // invoked on engine/audio threads; marshal in the UI

    // Surface (called from the UI thread). The engine takes ownership.
    void attachSurface(std::unique_ptr<render::PreviewRenderer> surface);
    void detachSurface();
    void resizeSurface(int width, int height);
    void requestRedraw();

    // Program + transport
    void setProgram(std::shared_ptr<const Program> program, TimePoint keepPosition = TimePoint{-1});
    [[nodiscard]] std::shared_ptr<const Program> program() const;
    // Plays at `rate` (1 = normal, 2/4/8 = fast forward, negative = reverse).
    // Audio is heard only at normal speed. Calling it while playing changes the
    // rate from the current position.
    void play(double rate = 1.0);
    void pause();
    void togglePlay();
    void stop();                         // pause and return to where play started
    void seek(TimePoint t, bool scrubbing = false);
    void stepFrames(int frames);
    void setQuality(PreviewQuality q);
    void setMuted(bool muted);
    // Loop playback: at the end of the range playback jumps back to its start.
    // in/out < 0 mean the start / end of the program.
    void setLoop(bool enabled, TimePoint in = TimePoint{-1}, TimePoint out = TimePoint{-1});
    // Viewer magnification: <= 0 fits the frame to the surface; 1 = one frame
    // pixel per screen pixel. Pan is in surface pixels from the centre.
    void setViewZoom(double zoom, int panX = 0, int panY = 0);

    [[nodiscard]] TimePoint position() const noexcept { return TimePoint{m_position.load()}; }
    [[nodiscard]] bool isPlaying() const noexcept { return m_playing.load(); }
    [[nodiscard]] double rate() const noexcept { return m_rate.load(); }
    [[nodiscard]] PreviewQuality quality() const noexcept { return m_quality.load(); }
    [[nodiscard]] PlaybackStats stats() const;
    [[nodiscard]] render::Compositor& compositor() noexcept { return *m_compositor; }
    [[nodiscard]] bool audioAvailable() const noexcept { return m_audio != nullptr; }

private:
    PlaybackEngine() = default;
    void threadMain();
    void renderOnce(TimePoint t, bool vsync);
    void ensureTarget(int frameW, int frameH);
    VideoReader& readerFor(const VideoLayerRequest& req);
    void pruneReaders();
    // Rasterised text layers, keyed by content hash; bounded LRU.
    std::shared_ptr<render::FrameTexture> textTextureFor(const TextParams& text, int frameW, int frameH);
    std::shared_ptr<render::FrameTexture> solidTextureFor(const ColorRgba& color);
    void notifyPosition(TimePoint t, bool force);
    void wake() { m_cv.notify_all(); }
    TimePoint clockNow() const;

    std::shared_ptr<hw::GpuDevice> m_gpu;
    std::unique_ptr<render::Compositor> m_compositor;
    std::unique_ptr<render::PreviewRenderer> m_surface;
    std::unique_ptr<render::RenderTarget> m_target;
    std::unique_ptr<audio::AudioOutput> m_audio;
    std::unique_ptr<AudioMixer> m_mixer;
    bool m_allowHardware = true;
    Callbacks m_callbacks;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    bool m_stop = false;
    bool m_redraw = false;
    bool m_surfaceDirty = false;
    bool m_retryRender = false;     // last paused frame was incomplete; poll the readers
    int m_retryCount = 0;
    static constexpr int kMaxRetries = 250;   // ~3 s at 12 ms
    int m_surfaceW = 0, m_surfaceH = 0;
    std::shared_ptr<const Program> m_program;
    // One reader per (media, clip): two clips of the same file may be visible
    // at once, and a reader that stays on its clip never has to seek far.
    struct ReaderEntry {
        std::string path;
        std::unique_ptr<VideoReader> reader;
        std::chrono::steady_clock::time_point lastUsed;
    };
    std::unordered_map<std::string, ReaderEntry> m_readers;
    struct TextEntry {
        std::shared_ptr<render::FrameTexture> texture;
        std::uint64_t lastUse = 0;
    };
    std::unique_ptr<render::TextRasterizer> m_text;     // created lazily on the render thread
    bool m_textUnavailable = false;
    std::unordered_map<std::uint64_t, TextEntry> m_textCache;
    std::unordered_map<std::uint32_t, std::shared_ptr<render::FrameTexture>> m_solidCache;   // BGRA colour -> 16x16 texture
    std::uint64_t m_textUseCounter = 0;
    std::vector<VideoLayerRequest> m_layerRequests;
    std::vector<VideoLayerRequest> m_prerollRequests;
    std::vector<render::Layer> m_layers;

    std::atomic<Ticks> m_position{0};
    std::atomic<bool> m_playing{false};
    std::atomic<PreviewQuality> m_quality{PreviewQuality::Auto};
    std::atomic<double> m_rate{1.0};
    bool m_loop = false;
    TimePoint m_loopIn{-1}, m_loopOut{-1};
    double m_viewZoom = 0.0;
    int m_panX = 0, m_panY = 0;
    TimePoint m_playOrigin;                 // program time when play started
    std::chrono::steady_clock::time_point m_playWall;
    TimePoint m_stopReturn;                 // where Stop returns to
    bool m_scrubbing = false;

    PlaybackStats m_stats;
    std::chrono::steady_clock::time_point m_lastStats;
    std::chrono::steady_clock::time_point m_lastPositionNotify;
    std::uint64_t m_fpsCounter = 0;
    std::chrono::steady_clock::time_point m_fpsWindowStart;
};

}  // namespace lf::playback
