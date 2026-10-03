#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/project/MediaItem.h"

#include <atomic>
#include <functional>
#include <string>

// Proxies: small H.264 copies of heavy media used only for preview. They are
// a cache keyed by the source file (path, size, modification time), so they
// need no project data and are shared by every project using the file.
namespace lf::exporting {

inline constexpr int kProxyHeight = 540;

// File name (no folder) of the proxy for a media item, e.g. "3fa2c19d0b7e4411_540p.mp4".
[[nodiscard]] std::string proxyFileName(const project::MediaItem& item, int height = kProxyHeight);
// Whether the item can have a proxy (has video, is not a still image).
[[nodiscard]] bool proxyApplies(const project::MediaItem& item) noexcept;

// Transcodes the video stream of `source` to `destination` (H.264 yuv420p,
// at most `height` lines, keyframe every half second, same timestamps; no
// audio - preview audio still comes from the original). Writes a partial
// file and renames it when done; progress is 0..1.
Status makeProxy(const std::string& source, const std::string& destination, int height, const std::function<void(double)>& progress,
                 const std::atomic<bool>& cancel);

}  // namespace lf::exporting
