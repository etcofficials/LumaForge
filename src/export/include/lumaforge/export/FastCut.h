#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/export/ExportSettings.h"

#include <atomic>
#include <functional>
#include <string>

// Lossless "fast cut": when the export range is a plain piece of one source
// file (no effects, colour, transforms, text, transitions, speed, audio
// changes, other clips...), the compressed streams are copied without
// re-encoding. Cuts land on the keyframe at or before the In point, so the
// result may start slightly earlier than the range.
namespace lf::exporting {

struct FastCutCheck {
    bool eligible = false;
    std::string reason;          // why not, for the UI ("RE-ENCODE: clip has a colour correction")
    std::string sourcePath;
    TimePoint sourceIn, sourceOut;   // media range to copy
    bool withAudio = false;
};

[[nodiscard]] FastCutCheck checkFastCut(const project::Project& project, const ExportSettings& settings);

// Copies the range into settings.outputPath (partial file + rename). Progress 0..1.
Status fastCut(const FastCutCheck& check, const ExportSettings& settings, const std::function<void(double)>& progress, const std::atomic<bool>& cancel);

}  // namespace lf::exporting
