#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/export/ExportSettings.h"
#include "lumaforge/media/Image.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace lf::hw { class GpuDevice; }

namespace lf::exporting {

struct ExportProgress {
    std::int64_t frame = 0;          // frames written so far
    std::int64_t totalFrames = 0;
    double elapsedSeconds = 0.0;
    double framesPerSecond = 0.0;    // render+encode speed
    [[nodiscard]] double fraction() const noexcept { return totalFrames > 0 ? static_cast<double>(frame) / static_cast<double>(totalFrames) : 0.0; }
    [[nodiscard]] double etaSeconds() const noexcept { return framesPerSecond > 0.0 ? static_cast<double>(totalFrames - frame) / framesPerSecond : -1.0; }
};

struct ExportResult {
    std::string path;
    std::int64_t frames = 0;
    double seconds = 0.0;            // wall time
    std::uint64_t bytes = 0;
    std::string videoEncoder;        // FFmpeg encoder actually used
    std::string audioEncoder;        // empty when there was no audio
    std::string note;                // e.g. an encoder fallback, for the user
    int width = 0, height = 0;
};

// Renders a project snapshot to a file: the same program and layer path as
// the preview (SequenceProgram -> LayerFactory -> Compositor), with every
// frame decoded exactly, mixed audio from the same AudioMixer, encoded with
// FFmpeg. Runs synchronously on the caller's thread (use a worker thread).
// The file is written as "<name>.partial<ext>" and renamed when complete;
// cancelled or failed exports leave nothing behind.
class Exporter {
public:
    // gpu may be null: a WARP (software) device is created for rendering.
    explicit Exporter(std::shared_ptr<hw::GpuDevice> gpu);
    ~Exporter();

    using ProgressFn = std::function<void(const ExportProgress&)>;
    Result<ExportResult> run(const project::Project& snapshot, const ExportSettings& settings, const ProgressFn& progress, const std::atomic<bool>& cancel);

    // One composited frame at sequence time t, at width x height (0 = project size).
    Result<media::Image> renderFrame(const project::Project& snapshot, TimePoint t, int width = 0, int height = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace lf::exporting
