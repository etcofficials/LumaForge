#pragma once

#include "lumaforge/media/MediaInfo.h"
#include "lumaforge/project/Ids.h"

#include <cstdint>
#include <string>

namespace lf::project {

// One entry in the media bin. The project stores a *reference* (path +
// fingerprint) to the file, never the file itself.
struct MediaItem {
    MediaId id = MediaId::Invalid;
    std::string path;            // absolute, UTF-8
    std::string displayName;     // defaults to the file name; user-renamable later
    media::MediaInfo info;       // probe result at import time (refreshed on relink)
    std::uint64_t fingerprintSize = 0;    // file size at import, for relinking
    std::int64_t fingerprintMtime = 0;    // modification time at import
    std::int64_t addedUnixTime = 0;
    bool offline = false;        // file missing at load time

    [[nodiscard]] media::MediaType type() const noexcept { return info.type; }
    friend bool operator==(const MediaItem&, const MediaItem&) = default;
};

}  // namespace lf::project
